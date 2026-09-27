// EE1c — 9회차 ② 레거시 발급이 `Company.TagType` 을 0(미지정)으로 **먼저 내린다**.
//  그 중간 상태(종류 미지정)를 네 기기와 두 발급 경로가 어떻게 다루는가.
//  C1 찢긴 발급이 남기는 TagType=0 카드가 실제로 생기는가(절단점 스윕)
//  C2 그 카드가 W·D·S·G 네 기기에서 전부 거부음으로 거부되고 카드가 바뀌지 않는가
//  C3 그 카드를 **다시 발급**할 수 있는가 — 레거시('S')·설정기(cfg_new_tag type1) 두 경로
//  C4 'C'(클리어) 발급도 같은 순서인가(찢기면 클리어가 아니라 미지정)
//  C5 설정기 cfg_new_tag 가 만드는 카드의 TagType 도 0 이다 = 미지정은 **정상 중간 상태**
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
static int tag_type(const SimCard &c)
{
    Company co{};
    memcpy(&co, c.data[SECTOR0_COMPANY], sizeof(co));
    return co.TagType;
}
static void as_server()
{
    deviceOption.SetType('S');
    hard_reset(false, 2);
    serial_inject("Z", 1);
    GUARDED(serialEvent());
    run_loops(1);
}
// 레거시 발급 명령 한 번(카드는 올려 둔 채)
static void issue(SimCard &c, const char *cmd, int16_t cutAt)
{
    c.removeAfterOps = cutAt;
    c.opCount = 0;
    card_place(&c);
    logs_clear();
    serial_inject(cmd, strlen(cmd));
    GUARDED(serialEvent());
    card_remove();
    run_loops(4);
    c.removeAfterOps = 0;
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    managerOption.SetData(mk, mn);

    // ── C1 찢긴 'M' 발급 절단점 스윕 — TagType=0 카드가 남는 자리 ──
    uint8_t nZero = 0, nOther = 0;
    uint16_t Kissue = 0;
    {
        as_server();
        make_tag(sc, 0x51, SCOPE_TYPE_TAG, 21, "SC0021", "S0021");
        set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
        issue(sc, "ZM7788;LEE;", 0);
        Kissue = sc.opCount;
        for (uint16_t n = 1; n <= Kissue; ++n)
        {
            make_tag(sc, 0x51, SCOPE_TYPE_TAG, 21, "SC0021", "S0021");
            set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
            issue(sc, "ZM7788;LEE;", (int16_t)n);
            const int t = tag_type(sc);
            if (t == 0) ++nZero;
            else if (t != SCOPE_TYPE_TAG && t != MANAGER_TYPE_TAG) ++nOther;
        }
        tlog("  C1 발급 동작 %u · 찢겨 TagType=0 으로 남은 절단점 %u · 그 밖의 값 %u\n",
             (unsigned)Kissue, (unsigned)nZero, (unsigned)nOther);
        CHECK(Kissue > 0 && nZero > 0, "C1 양성대조: 찢긴 발급이 실제로 TagType=0 카드를 남긴다");
        CHECK(nOther == 0, "C1 찢긴 발급이 0·스코프·담당자 밖의 종류를 남기지 않는다");
    }

    // ── C2 TagType=0 카드를 네 기기에 — 전부 거부음 + 카드 불변 ──
    uint8_t nRejected = 0;
    bool unchanged = true;
    {
        static const char types[4] = {'W', 'D', 'S', 'G'};
        for (uint8_t i = 0; i < 4; ++i)
        {
            make_tag(sc, 0x52, 0, 21, "SC0021", "S0021");    // 종류 미지정
            set_process(sc, Process{1, 1, 1, 1, 9, false, 0, 2});
            const Process p0 = get_process(sc);
            if (types[i] == 'S') as_server();
            else                 as_type(types[i]);
            logs_clear();
            touch(sc, 2, 4);
            const bool rej = lcd_has("Invalid Tag Type");
            if (rej) ++nRejected;
            const Process p1 = get_process(sc);
            if (memcmp(&p0, &p1, 9) != 0 || tag_type(sc) != 0) unchanged = false;
            tlog("   C2 %c: 거부=%u lcd=[%.28s]\n", types[i], (unsigned)rej, g_lcdLog);
        }
        tlog("  C2 네 기기 거부 %u/4 · 카드 불변=%u\n", (unsigned)nRejected, (unsigned)unchanged);
        CHECK(nRejected == 4, "C2 종류 미지정 카드는 W·D·S·G 네 기기 모두 'Invalid Tag Type' 으로 거부한다");
        CHECK(unchanged, "C2 거부는 카드를 건드리지 않는다(공정·종류 불변)");
    }

    // ── C3 미지정 카드 재발급 — 레거시 · 설정기 두 경로 ──
    bool reLegacy = false, reJson = false;
    {
        as_server();
        make_tag(sc, 0x53, 0, 0, "", "");                    // 종류 미지정 · 신원 없음
        issue(sc, "ZS21;S0021;", 0);
        reLegacy = (tag_type(sc) == SCOPE_TYPE_TAG) && lcd_has("new tag");
        tlog("   C3 레거시 재발급: 종류=%d lcd=[%.28s]\n", tag_type(sc), g_lcdLog);

        make_tag(sc, 0x54, 0, 0, "", "");
        card_place(&sc);
        run_loops(2);
        logs_clear();
        const char *j = "{\"cmd\":\"cfg_new_tag\",\"type_id\":1}";
        serial_inject(j, strlen(j));
        GUARDED(serialEvent());
        card_remove();
        run_loops(4);
        reJson = lcd_has("tag created");
        tlog("   C3 설정기 재발급(type1): lcd=[%.28s] 종류=%d\n", g_lcdLog, tag_type(sc));
        CHECK(reLegacy, "C3 종류 미지정 카드는 레거시 발급으로 되살린다");
        CHECK(reJson, "C3 종류 미지정 카드는 설정기 cfg_new_tag(type1)도 받아 준다");
    }

    // ── C4 'C'(클리어) 발급도 같은 순서인가 ──
    uint8_t nZeroC = 0, nClearEarly = 0;
    {
        as_server();
        make_tag(sc, 0x55, SCOPE_TYPE_TAG, 21, "SC0021", "S0021");
        set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
        issue(sc, "ZC", 0);
        const uint16_t Kc = sc.opCount;
        const bool ctl = (tag_type(sc) == CLEAR_TYPE_TAG);
        for (uint16_t n = 1; n < Kc; ++n)
        {
            make_tag(sc, 0x55, SCOPE_TYPE_TAG, 21, "SC0021", "S0021");
            set_process(sc, Process{1, 1, 1, 1, 9, false, 0, 2});   // 옛 공정이 남은 카드
            issue(sc, "ZC", (int16_t)n);
            if (tag_type(sc) == 0) ++nZeroC;
            // '반쪽 클리어' = 종류는 클리어인데 앞 단계(옛 공정 소거)가 아직 안 끝났다
            const Process p = get_process(sc);
            Process zero{};
            if (tag_type(sc) == CLEAR_TYPE_TAG && memcmp(&p, &zero, 9) != 0) ++nClearEarly;
        }
        tlog("  C4 'C' 발급 동작 %u · 온전 발급=%u · 찢겨 0 %u · 반쪽 클리어 %u\n",
             (unsigned)Kc, (unsigned)ctl, (unsigned)nZeroC, (unsigned)nClearEarly);
        CHECK(ctl, "C4 양성대조: 온전한 'C' 발급은 클리어 태그가 된다");
        CHECK(nZeroC > 0, "C4 양성대조: 'C' 발급도 찢기면 종류 미지정으로 남는다(같은 순서)");
        CHECK(nClearEarly == 0, "C4 찢긴 'C' 발급이 '종류만 클리어 · 옛 공정 그대로' 카드를 남기지 않는다");
    }

    // ── C5 설정기 cfg_new_tag 가 만드는 카드의 종류는 0 이다(= 미지정은 정상 중간 상태) ──
    {
        as_server();
        make_tag(sc, 0x56, SCOPE_TYPE_TAG, 21, "SC0021", "S0021");
        card_place(&sc);
        run_loops(2);
        logs_clear();
        const char *j = "{\"cmd\":\"cfg_new_tag\",\"type_id\":1}";
        serial_inject(j, strlen(j));
        GUARDED(serialEvent());
        card_remove();
        run_loops(4);
        tlog("  C5 cfg_new_tag(type1) 뒤 종류=%d lcd=[%.28s]\n", tag_type(sc), g_lcdLog);
        CHECK(lcd_has("tag created") && tag_type(sc) == 0,
              "C5 설정기 발급이 끝난 카드의 종류는 0 이다 — 미지정은 발급 절차의 정상 중간 상태");
    }

    done();
    for (;;) {}
}
