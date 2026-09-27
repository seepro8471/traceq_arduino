// FF1a — 10차 ③ 선행 소거 조건 `prevStatus == 0` 의 **여집합 전수**.
//  주석은 "레거시 2/3 은 update_process 가 처리한다" 고 적었지만 {0,1} 의 여집합은 {2,3} 이 아니다.
//  G1 Status 전수(0·1·2·3·4·200)에서 "이 주기의 Status 가 1 이 아닌데 블록 8·9 에 옛 환자가 남는" 자리
//  G2 그 자리가 실제로 서버 덤프에 옛 등록번호를 내보내는가(피해 — t_dd L1 과 같은 방식)
//  G3 그 자리의 접촉 동작 수가 t_ops 상한(40) 안인가
#include "common.h"

static SimCard sc;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', '1'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};

static void as_type(char t)
{
    deviceOption.SetType(t);
    hard_reset(false, 2);
    managerOption.SetData(mk, mn);
}

// 옛 환자가 블록 8·9 에 살아 있는 스코프(찢긴 게이트웨이·덤프 뒤 소거 실패로 실제로 생기는 모습)
static void prior_patient(SimCard &c, uint8_t uid, int no, uint8_t status, const char *key)
{
    char id[10], ser[10];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(c, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(c, Process{status, 0, 0, 0, 7, false, 0, 0});
    put_block(c, SECTOR2_PATIENT_KEY,  key, (uint8_t)strlen(key));
    put_block(c, SECTOR2_PATIENT_NAME, "OLDPATIENT", 10);
}

// 한 주기(세척→소독)를 돌고 서버 덤프. 반환 = 덤프가 나갔는가(줄은 serial_has 로 본다).
static bool cycle_then_dump(SimCard &c)
{
    as_type('W');
    touch(c, 2, 4);                        // 세척 시작
    sim_advance_ms(20UL * 60 * 1000);
    touch(c, 2, 4);                        // 세척 종료
    as_type('D');
    touch(c, 2, 4);                        // 소독 시작
    sim_advance_ms(20UL * 60 * 1000);
    touch(c, 2, 4);                        // 소독 종료
    deviceOption.SetType('S');
    hard_reset(false, 2);
    serial_inject("Z", 1);
    GUARDED(serialEvent());
    run_loops(1);
    logs_clear();
    serial_inject("Z", 1);
    touch(c, 2, 4);
    return serial_has("Ok!");
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    managerOption.SetData(mk, mn);

    // ── G1 Status 전수: 세척 시작 뒤 "Status != 1 인데 옛 환자가 남는" 자리 ──
    const uint8_t sts[6] = {0, 1, 2, 3, 4, 200};
    uint8_t nLeak = 0, leakStatus[6]{}, nLeaks = 0;
    uint16_t maxOps = 0, opsAt[6]{};
    {
        for (uint8_t i = 0; i < 6; ++i)
        {
            as_type('W');
            prior_patient(sc, (uint8_t)(0x50 + i), 50 + i, sts[i], "OLDKEY99");
            sc.opCount = 0;
            logs_clear();
            touch(sc, 2, 4);
            const Process p = get_process(sc);
            const bool keyEmpty  = sc.data[SECTOR2_PATIENT_KEY][0] == 0;
            const bool nameEmpty = sc.data[SECTOR2_PATIENT_NAME][0] == 0;
            opsAt[i] = sc.opCount;
            if (sc.opCount > maxOps) maxOps = sc.opCount;
            const bool leak = (p.Status != 1) && !keyEmpty;
            if (leak) { leakStatus[nLeaks < 6 ? nLeaks : 5] = sts[i]; ++nLeaks; ++nLeak; }
            tlog("  G1 prev=%3u -> Status=%u ws=%u rw=%u · 키비움=%u 이름비움=%u · 동작 %u%s\n",
                 (unsigned)sts[i], (unsigned)p.Status, (unsigned)p.WashingStatus, (unsigned)p.Rewrite,
                 (unsigned)keyEmpty, (unsigned)nameEmpty, (unsigned)sc.opCount, leak ? "   <== 옛 환자 잔존" : "");
        }
        tlog("  G1 '환자정보 없음(Status!=1)인데 옛 환자 잔존' = %u 자리 (최대 동작 %u)\n",
             (unsigned)nLeak, (unsigned)maxOps);
        CHECK(opsAt[0] > 0 && opsAt[5] > 0, "G1 양성대조: 모든 Status 표본에서 세척 시작이 실제로 돌았다");
        CHECK(nLeak == 0,
              "G1 세척 시작 뒤 'Status != 1(환자정보 없음)인데 블록 8·9 에 옛 환자' 태그가 남지 않는다");
        CHECK(maxOps <= 40, "G1 모든 Status 갈래가 접촉 예산(40) 안");
    }

    // ── G2 피해: 그 태그가 한 주기를 돌면 옛 등록번호가 서버 덤프로 나가는가 ──
    //    OLDKEY99 = 4F4C444B45593939 · REALKEY1 = 5245414C4B455931
    {
        bool sent200 = false, sent0 = false, sentReal = false;
        prior_patient(sc, 0x58, 58, 200, "OLDKEY99");
        cycle_then_dump(sc);
        sent200 = serial_has("4F4C444B45593939");

        sim_advance_ms(60UL * 1000);
        prior_patient(sc, 0x59, 59, 0, "OLDKEY99");
        cycle_then_dump(sc);
        sent0 = serial_has("4F4C444B45593939");

        sim_advance_ms(60UL * 1000);
        prior_patient(sc, 0x5A, 60, 1, "REALKEY1");
        cycle_then_dump(sc);
        sentReal = serial_has("5245414C4B455931");

        tlog("  G2 덤프에 옛 등록번호 — Status=200 전송=%u · Status=0 전송=%u · 진짜 환자(Status=1) 전송=%u\n",
             (unsigned)sent200, (unsigned)sent0, (unsigned)sentReal);
        CHECK(sentReal, "G2 양성대조: 환자정보 있는 태그의 등록번호는 덤프에 그대로 나간다");
        CHECK(!sent0, "G2 대조: Status=0 + 옛 환자는 덤프에 나가지 않는다(선행 소거가 듣는다)");
        CHECK(!sent200,
              "G2 Status 가 0·1·2·3 밖인 태그의 옛 등록번호도 덤프에 나가지 않는다");
    }

    done();
    for (;;) {}
}
