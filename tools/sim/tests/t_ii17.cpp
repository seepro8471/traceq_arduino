// 15회차 소리 잠금 — 13차 HH2 '미확인' 중 읽기로 소리 CHECK 를 못 찾은 세 자리(III-F 지목 · 초안 t_iiif3).
//  L-a 일회성 담당자 거부(RecordProcessor::try_load_manager_data) — 형제 is_valid 의 거부는 t_notify 가 소리까지 본다.
//  L-b 부팅 선택창(main.cpp ask_erase_settings) — 커서 이동 30ms 1회 · 확정 50ms 1회.
//  L-c 설정기 발급(cfg_new_tag · SerialProcessor::NewTag) 대기 비프 — 1초마다 50ms. 형제 레거시 발급 대기는 t_hh2s S18.
//  소리는 buzz_count(펄스 길이) · 글자는 lcd_has 로만 묻는다(제품 행위).
#include "common.h"

static SimCard sc, mgr;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', '1'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");

    // ── L-a 일회성 ON · 재부팅 직후(RAM 표지 없음) 담당자 태그 없이 스코프 시작 → 거부 ──
    {
        managerOption.SetData(mk, mn);                         // 등록된 담당자는 있다 — is_valid 의 거부가 아니다
        recordOption.SetManagerDisposability(true);
        power_restore(); hard_reset(false, 2);
        make_tag(sc, 0x51, SCOPE_TYPE_TAG, 51, "SC0051", "S0051");
        logs_clear(); buzz_clear();
        touch(sc);
        const uint8_t p60 = buzz_count(60), p600 = buzz_count(600), p50 = buzz_count(50),
                      p100 = buzz_count(100), p400 = buzz_count(400);
        tlog("  L-a 일회성 거부: 60x%u 600x%u 50x%u 100x%u 400x%u lcd=%d RW=%u\n", p60, p600, p50, p100, p400,
             (int)lcd_has("No Manager Info"), (unsigned)get_process(sc).Rewrite);
        CHECK(lcd_has("No Manager Info") && get_process(sc).Rewrite == 0,
              "L-a 전제: 일회성 ON 에 담당자 태그 없이 대면 시작을 거부한다(글자 No Manager Info · 기록 없음)");
        CHECK(p60 == 2 && p600 == 1 && p50 == 0 && p100 == 0 && p400 == 0,
              "L-a 일회성 담당자 거부 = 거부음(60ms 2회 + 600ms 1회)만 — 성공음·실패음·환자정보없음 소리 없음");

        // 대조: 담당자 태그를 대고 나면 같은 스코프가 시작된다(거부가 담당자 없음 때문이었다)
        touch(mgr);
        logs_clear(); buzz_clear();
        touch(sc);
        tlog("  L-a 대조(담당자 뒤): RW=%u 600x%u lcd=%d\n", (unsigned)get_process(sc).Rewrite, buzz_count(600),
             (int)lcd_has("No Manager Info"));
        CHECK(get_process(sc).Rewrite == 1 && buzz_count(600) == 0 && !lcd_has("No Manager Info"),
              "L-a 대조: 담당자 태그를 댄 뒤엔 같은 스코프 시작이 커밋되고 거부음이 없다");
        recordOption.SetManagerDisposability(false);
    }

    // ── L-b 부팅 선택창: 켤 때 RIGHT(4회 읽기) → 뗌 → RIGHT(초기화 쪽) → LEFT(유지 쪽) → MENU(확정) ──
    {
        logs_clear(); buzz_clear();
        buttons_script("RRRRrRLS");
        power_restore(); hard_reset(false, 1);
        buttons_script("");
        const uint8_t p30 = buzz_count(30), p50 = buzz_count(50);
        tlog("  L-b 부팅 선택: 30x%u 50x%u 물음=%d 초기화쪽=%d 초기화함=%d\n", p30, p50, (int)lcd_has("Keep settings?"),
             (int)lcd_has("> Erase all"), (int)lcd_has("Initializing"));
        CHECK(lcd_has("Keep settings?") && lcd_has("> Erase all") && !lcd_has("Initializing"),
              "L-b 전제: 선택창이 뜨고 커서가 초기화 쪽으로 갔다가 유지로 확정됐다(설정 유지)");
        CHECK(p30 == 2, "L-b 선택 이동은 누를 때마다 30ms 1회(오른쪽·왼쪽 = 2회)");
        CHECK(p50 == 2, "L-b 확정 50ms 1회 + 부팅 완료 50ms 1회 = 50ms 2회");
    }

    // ── L-c 설정기 발급 대기(태그 없음): 4초 동안 1초마다 50ms → 실패음 ──
    {
        deviceOption.SetType('S');
        power_restore(); hard_reset(false, 2);
        logs_clear(); buzz_clear();
        const char cmd[] = "{\"cmd\":\"cfg_new_tag\",\"type_id\":1}";
        serial_inject(cmd, sizeof(cmd) - 1);
        GUARDED(serialEvent());
        const uint8_t p50 = buzz_count(50), p100 = buzz_count(100), p500 = buzz_count(500);
        tlog("  L-c cfg_new_tag 대기: 50x%u 100x%u 500x%u 실패=%d 발급=%d\n", p50, p100, p500,
             (int)lcd_has("timeout or error"), (int)lcd_has("tag created"));
        CHECK(lcd_has("timeout or error") && !lcd_has("tag created") && p100 == 4 && p500 == 0,
              "L-c 전제: 태그 없이 4초 대기 뒤 실패 안내(100ms 4회) · 발급 성공음 없음");
        // 시뮬 millis() 는 호출마다 1ms 라 비프 간격이 약간 늘어난다 — 횟수는 범위로(지금 3).
        CHECK(p50 >= 2 && p50 <= 4,
              "L-c 설정기 발급 대기 비프 = 1초마다 50ms(4초에 2~4회)");
    }

    done();
    for (;;) {}
}
