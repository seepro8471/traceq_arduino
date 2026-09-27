// EE1e — 9회차 ① 선행 쓰기가 `update_process` 의 **레거시 Status 2/3 갈래**와 어떻게 만나는가.
//  [5차 판정 · 재론 금지] "2.0 은 Status 에 0·1 만 쓴다" 가 전제다 — 이 시험은 그 전제가 깨졌을 때의
//  결과를 **사실로만** 적는다(전제가 맞으면 죽은 코드다).
//  E1 Status=3(레거시 '환자정보 있음') 태그: 선행 소거가 블록 8·9 를 비운 뒤 커밋이 Status=1 을 세우는가
//     = "환자정보 있음인데 블록은 빈" 태그(SerialProcessor 주석이 만들지 않겠다고 적은 상태)가 생기나
//  E2 Status=2 태그: 선행 소거와 update_process 의 Clear×4 가 같은 블록을 두 번 지우나(동작 수)
#include "common.h"

static SimCard mgr, sc;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', '1'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};

static void prior_patient(SimCard &c, uint8_t uid, uint8_t status)
{
    make_tag(c, uid, SCOPE_TYPE_TAG, 31, "SC0031", "S0031");
    set_process(c, Process{status, 0, 0, 0, 7, false, 0, 0});
    put_block(c, SECTOR2_PATIENT_KEY,  "OLDKEY99", 8);
    put_block(c, SECTOR2_PATIENT_NAME, "OLDPATIENT", 10);
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    managerOption.SetData(mk, mn);
    (void)mgr;

    // ── E1 Status=3 ──
    {
        prior_patient(sc, 0x31, 3);
        sc.opCount = 0;
        logs_clear();
        touch(sc);
        const Process p = get_process(sc);
        const bool keyEmpty  = sc.data[SECTOR2_PATIENT_KEY][0] == 0;
        const bool nameEmpty = sc.data[SECTOR2_PATIENT_NAME][0] == 0;
        tlog("  E1 Status 3 -> %u · 키비움=%u 이름비움=%u · 동작 %u · 환자없음안내=%u\n",
             (unsigned)p.Status, (unsigned)keyEmpty, (unsigned)nameEmpty, (unsigned)sc.opCount,
             (unsigned)lcd_has("No Patient Info"));
        CHECK(p.WashingStatus == 1 && p.Rewrite == 1, "E1 양성대조: Status=3 태그도 세척 시작이 커밋된다");
        CHECK(!(p.Status == 1 && keyEmpty && nameEmpty),
              "E1 '환자정보 있음(Status=1)인데 등록번호·이름은 빈' 태그를 만들지 않는다");
    }

    // ── E2 Status=2 ──
    {
        prior_patient(sc, 0x32, 2);
        sc.opCount = 0;
        logs_clear();
        touch(sc);
        const Process p = get_process(sc);
        tlog("  E2 Status 2 -> %u · 키비움=%u · 동작 %u\n", (unsigned)p.Status,
             (unsigned)(sc.data[SECTOR2_PATIENT_KEY][0] == 0), (unsigned)sc.opCount);
        CHECK(p.WashingStatus == 1 && p.Rewrite == 1 && p.Status == 0,
              "E2 Status=2 태그는 세척 시작에서 0 으로 정리되고 커밋된다");
        // Status==2 는 1.0 에서 온 태그의 **전환 1회**다(2.0 은 Status 에 0·1 만 쓴다) — 레거시 갈래가
        //  블록 5·8·9 와 섹터15 3블록을 비우므로 매 주기 갈래(40 · t_ops·t_ff1a·t_gg1b)보다 무겁다.
        CHECK(sc.opCount <= 45, "E2 Status=2(1.0 전환 태그 1회) 갈래는 접촉 예산 45 안");
    }

    // ── E3 대조: Status=0 태그는 선행 소거로 옛 환자가 비워진다(9차 봉합의 본래 목적) ──
    {
        prior_patient(sc, 0x33, 0);
        logs_clear();
        touch(sc);
        const bool keyEmpty = sc.data[SECTOR2_PATIENT_KEY][0] == 0;
        tlog("  E3 Status 0: 키비움=%u Status=%u\n", (unsigned)keyEmpty, (unsigned)get_process(sc).Status);
        CHECK(keyEmpty, "E3 대조: Status=0 + 옛 환자 태그는 세척 시작이 블록 8·9 를 비운다");
    }

    // ── E4 ① 의 가중 인자: **일회성 담당자** 현장에서 찢긴 더블터치 재시작 뒤 회복 접촉이 거부되는가 ──
    //    재시작이 찢겨 태그가 '공정 없음'(Rewrite=0)이 되면 다음 접촉은 isEnd=false 인데, 일회성 표지는
    //    1차 시작 커밋에서 이미 소모됐다 → `No Manager Info` 거부. 담당자 카드를 다시 대야 한다.
    {
        make_tag(mgr, 0x05, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
        make_tag(sc, 0x34, SCOPE_TYPE_TAG, 34, "SC0034", "S0034");
        set_process(sc, Process{1, 0, 0, 0, 0, false, 0, 0});
        recordOption.SetManagerDisposability(true);
        touch(mgr);                                   // 일회성 담당자 등록
        touch(sc, 1, 4);                              // 1차 세척 시작(커밋) — 일회성 소모
        const Process p1 = get_process(sc);
        // 태그를 '공정 없음'(찢긴 재시작의 결과)으로 직접 만든다 — 절단점에 기대지 않는다
        set_process(sc, Process{1, 0, 0, 0, 0, false, 0, 0});
        logs_clear();
        touch(sc, 1, 4);                              // 회복 접촉
        const Process p2 = get_process(sc);
        tlog("  E4 1차 ws=%u rw=%u · 회복 접촉 뒤 ws=%u rw=%u · 거부=%u\n",
             (unsigned)p1.WashingStatus, (unsigned)p1.Rewrite,
             (unsigned)p2.WashingStatus, (unsigned)p2.Rewrite,
             (unsigned)lcd_has("No Manager Info"));
        recordOption.SetManagerDisposability(false);
        CHECK(p1.WashingStatus == 1 && p1.Rewrite == 1, "E4 양성대조: 일회성 담당자로 1차 세척 시작이 커밋된다");
        // [10차 판정] 이 상태를 만들던 길(찢긴 더블터치 재시작)은 `isRestart` 로 닫혔다. 남은 길은 서버 덤프
        //  뒤의 새 주기뿐이고, 그때 담당자 카드를 다시 대는 것은 **일회성의 설계**다(등록당 한 번).
        //  그래서 요구가 아니라 **사실**로 잠근다 — 일회성이 꺼진 현장에서는 이 거부가 아예 없다.
        CHECK(p2.WashingStatus == 0 && lcd_has("No Manager Info"),
              "E4 사실: 일회성 담당자가 소모된 뒤 '공정 없음' 태그의 회복 접촉은 담당자 카드를 다시 요구한다");
    }

    done();
    for (;;) {}
}
