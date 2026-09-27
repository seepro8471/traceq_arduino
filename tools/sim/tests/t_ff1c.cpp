// FF1c — `t_ee2r` 가 **못 잠근 것**: 10차 판정 주석의 핵심 주장
//   *"슬롯 확보를 커밋 앞으로 옮기면 나머지 41자리에 유령 host(과소 계수)가 생긴다"* 에 행위 잠금이 없다.
//  t_ee2r 은 '커밋됐는데 슬롯이 없는' 상태를 **직접 만들어** 회복만 본다 → 그 상태를 **만드는 순서**를
//  바꿔도(= 사장님이 반대한 수정) 35종 전부 초록이다. 이 시험이 그 자리를 잠근다.
//  J1 소독 시작 절단점 전수 — 커밋되지 않은 시작이 host 슬롯을 남기는 자리가 있는가(유령 host)
//  J2 그 결과 같은 창의 다음 스코프가 guest 로 눌려 **배치가 0회**로 세어지는가(과소 계수)
#include "common.h"

static SimCard a, b;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', '1'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};

static int group_of(const SimCard &c)
{
    DisinfectionDetail d{};
    memcpy(&d, c.data[SECTOR14_DISINFECTION_DETAIL], sizeof(d));
    return d.GroupNumber;
}
static void washed(SimCard &c, uint8_t uid, int no)
{
    char id[10], ser[10];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(c, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(c, Process{1, 0, 1, 0, 1, false, 0, 0});
    set_record(c, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(c, SECTOR3_WASHING_END,   1, rel_date(9, 4, 0));
}
static void as_disinfector()
{
    deviceOption.SetType('D');
    hard_reset(false, 2);                     // ★RAM 의 host·시간창이 비어 있는 상태로
    managerOption.SetData(mk, mn);
    disinfectionOption.SetSimultaneousDisinfectionSlot(2);
    disinfectionOption.SetSimultaneousDisinfectionDelay(5);
}

int main()
{
    rtc_set(rel_date(11, 0, 0));
    boot('D');

    // ── 양성대조: 온전한 흐름 — 딜레이 안 둘째 스코프는 guest · 배치 1회 ──
    uint16_t K = 0;
    bool ctl = false;
    {
        as_disinfector();
        rtc_set(rel_date(11, 0, 0));
        washed(a, 0x31, 31);
        a.opCount = 0;
        touch(a, 2, 4);
        K = a.opCount;
        const uint8_t c0 = disinfectionOption.GetCount();
        sim_advance_ms(60UL * 1000);
        washed(b, 0x32, 32);
        touch(b, 2, 4);
        ctl = (group_of(a) == 1 && group_of(b) == 2 && disinfectionOption.GetCount() == c0);
        tlog("  J0 양성대조 K=%u · a그룹=%d b그룹=%d · 배치 횟수 %u->%u\n", (unsigned)K,
             group_of(a), group_of(b), (unsigned)c0, (unsigned)disinfectionOption.GetCount());
        CHECK(K > 0 && ctl,
              "J0 양성대조: 온전한 host 시작 뒤 딜레이 안 둘째 스코프는 guest 이고 배치는 1회로 센다");
    }

    // ── J1·J2 절단점 전수 ──
    uint8_t nGhost = 0, nZeroBatch = 0, nCommitted = 0;
    {
        for (uint16_t n = 0; n <= K; ++n)
        {
            as_disinfector();
            rtc_set(rel_date(11, 0, 0));
            const uint8_t cBefore = disinfectionOption.GetCount();
            washed(a, 0x31, 31);
            a.removeAfterOps = (int16_t)n;
            logs_clear();
            touch(a, 2, 4);
            a.removeAfterOps = 0;
            const Process pa = get_process(a);
            const bool committed = (pa.Rewrite == 2 && pa.DisinfectionCount >= 1);
            if (committed) ++nCommitted;

            sim_advance_ms(60UL * 1000);                 // 딜레이(5분) 안
            washed(b, 0x32, 32);
            touch(b, 2, 4);
            const uint8_t cAfter = disinfectionOption.GetCount();
            // 유령 host = a 의 시작이 태그에 커밋되지 않았는데 리더 RAM 이 host 를 쥐고 있어
            //   같은 창의 b 가 guest 로 눌린 자리
            if (!committed && group_of(b) == 2)
            {
                ++nGhost;
                if (nGhost <= 3)
                    tlog("   J1 유령 host n=%u (a rw=%u dc=%u · b그룹=%d · 횟수 %u->%u)\n", (unsigned)n,
                         (unsigned)pa.Rewrite, (unsigned)pa.DisinfectionCount, group_of(b),
                         (unsigned)cBefore, (unsigned)cAfter);
                if (cAfter == cBefore) ++nZeroBatch;     // 아무 스코프도 횟수를 올리지 못한 배치
            }
        }
        tlog("  J1 절단점 %u · 커밋된 자리 %u · 유령 host %u · 그중 배치 0회 %u\n",
             (unsigned)(K + 1), (unsigned)nCommitted, (unsigned)nGhost, (unsigned)nZeroBatch);
        CHECK(nCommitted > 0, "J1 양성대조: 절단점 스윕 안에 실제로 커밋되는 자리가 있다");
        CHECK(nGhost == 0,
              "J1 커밋되지 않은 소독 시작은 host 슬롯을 남기지 않는다(같은 창의 다음 스코프가 guest 로 눌리지 않는다)");
        CHECK(nZeroBatch == 0,
              "J2 그래서 '아무도 소독 횟수를 올리지 못한 배치'(과소 계수 = 오염된 액으로 계속 소독)가 없다");
    }

    disinfectionOption.SetSimultaneousDisinfectionSlot(1);
    done();
    for (;;) {}
}
