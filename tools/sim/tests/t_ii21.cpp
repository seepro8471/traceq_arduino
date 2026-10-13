// 업로더(uploader/traceq_fw_upload.py · sync_clock · 사장님 10-11)가 보내는 바이트의 펌웨어 쪽 계약.
//  업로더는 펌웨어를 올린 직후 STX{"cmd":"cfg_get_config"}ETX 로 부팅이 끝났는지 묻고(응답 = STX{json}ETX),
//  STX{"cmd":"cfg_set_date_time","device_date_time":"YYYY-MM-DD hh:mm:ss"}ETX 로 리더기 시계를 PC 시각으로 맞춘 뒤,
//  다시 설정 읽기로 살아 있음을 본다. 네 타입 전부 · 서버는 미인증('Z' 전)에서도 받아야 한다.
//  (업로더 쪽 바이트는 uploader/test_clock_sync.py ⑧ 이 같은 글자로 잠근다.)
#include "common.h"

static bool out_has_frame(char type)
{
    // 응답은 STX '{' 로 시작해 ETX 로 끝나고, 타입 글자가 실려 있다(업로더가 "타입·번호" 를 보여 준다)
    char key[24];
    snprintf(key, sizeof(key), "\"device_type\":\"%c\"", type);
    const char *stx = strchr(g_serialOut, 0x02);
    const size_t n = strlen(g_serialOut);
    return stx != nullptr && stx[1] == '{' && n > 0 && g_serialOut[n - 1] == 0x03 && strstr(g_serialOut, key) != nullptr;
}

int main()
{
    rtc_set(rel_date(9, 0, 0));
    boot('W');

    static const char GET[] = "\x02{\"cmd\":\"cfg_get_config\"}\x03";
    const char types[] = {'W', 'D', 'G', 'S'};
    for (uint8_t i = 0; i < 4; ++i)
    {
        const char t = types[i];
        power_restore(); deviceOption.SetType(t); deviceOption.SetNumber(3);
        rtc_set(rel_date(9, 0, 0));
        hard_reset(false, 2);                       // 서버도 'Z' 인증 없이(업로더는 'Z' 를 안 보낸다)

        // ① 설정 읽기 — 응답 프레임
        logs_clear();
        serial_inject(GET, sizeof(GET) - 1);
        GUARDED(serialEvent());
        const bool framed = out_has_frame(t);
        tlog("  [%c] 설정 읽기 응답: 프레임=%d 길이=%u\n", t, framed, (unsigned)strlen(g_serialOut));
        CHECK(framed, "① 업로더의 설정 읽기(STX{json}ETX)에 타입이 실린 STX{json}ETX 로 답한다(네 타입 · 서버 미인증 포함)");

        // ② 시각 — 리더기 시계가 그 시각이 되고 안내음 1000×1 · `updated`
        char js[96];
        char frame[100];
        rel_json_set_time(js, sizeof(js), 14, 3, 22);
        snprintf(frame, sizeof(frame), "\x02%s\x03", js);
        logs_clear(); buzz_clear();
        serial_inject(frame, strlen(frame));
        GUARDED(serialEvent());
        const DateTime now = rtc.GetCurrentDateTime();
        tlog("  [%c] 시각 맞춤: %02u:%02u:%02u 1000=%u 100=%u updated=%d\n", t, now.hour(), now.minute(), now.second(),
             (unsigned)buzz_count(1000), (unsigned)buzz_count(100), lcd_has("updated"));
        CHECK(now.year() == TRACEQ_RELEASE_YEAR && now.hour() == 14 && now.minute() == 3 && now.second() >= 22 && now.second() <= 26 &&
              buzz_count(1000) == 1 && buzz_count(100) == 0 && lcd_has("updated"),
              "② 업로더의 시각 프레임이 리더기 시계를 그 시각으로 맞춘다(안내음 1000×1 · updated · 네 타입)");

        // ③ 시각 뒤의 설정 읽기에도 답한다(업로더의 확인)
        logs_clear();
        serial_inject(GET, sizeof(GET) - 1);
        GUARDED(serialEvent());
        CHECK(out_has_frame(t), "③ 시각을 맞춘 뒤의 설정 읽기에도 답한다(업로더의 확인 단계)");
        // ③b (18차) 그 응답에 리더기의 **지금 시계**가 실린다 — 업로더·설정기가 되읽어 "맞췄다" 를 사실로 확인한다(시각 명령에는 응답이 없다)
        char want[48];
        snprintf(want, sizeof(want), "\"device_date_time\":\"%04u-%02u-%02u 14:03:2", (unsigned)TRACEQ_RELEASE_YEAR,
                 (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
        tlog("  [%c] 되읽기: %d\n", t, strstr(g_serialOut, want) != nullptr);
        CHECK(strstr(g_serialOut, want) != nullptr, "③b 설정 읽기 응답에 방금 맞춘 시계(device_date_time · YYYY-MM-DD hh:mm:ss)가 실린다(네 타입)");
    }

    // ④ setup 이 끝나기 전에 도착한 설정 읽기는 수신 버퍼에 남아 부팅 뒤에 답한다 — 업로더는 그 응답으로 "준비됨" 을 안다
    //  (한계: 이 시험은 도장이 같은 부팅이라 `Keep settings?` 갈래를 안 돌고, 가짜 Serial.begin 은 버퍼를 안 건드려 begin 전 바이트 유실은 못 본다
    //   — 업로더는 응답이 올 때까지 다시 묻는다)
    {
        power_restore(); deviceOption.SetType('D');
        logs_clear();
        serial_inject(GET, sizeof(GET) - 1);        // setup 전에 도착
        hard_reset(false, 0);                       // setup 만(시뮬 수신 버퍼는 리셋이 안 지운다 — 실물 UART 는 Serial.begin 뒤의 바이트를 쌓는다)
        GUARDED(serialEvent());
        CHECK(out_has_frame('D'), "④ 부팅 중에 온 설정 읽기도 부팅이 끝나면 답한다(업로더의 준비 신호)");
    }

    // ④b(19차) 설정기(ArduinoConfigurator · json.dumps 기본 구분자 = 빈칸이 든 `": "`·`", "`)의 꼴도 같은 계약 —
    //  업로더는 빈칸 없는 꼴만 보낸다. 설정기 쪽 바이트는 ArduinoConfigurator/test_config.py 가 같은 글자로 잠근다.
    {
        power_restore(); deviceOption.SetType('W');
        rtc_set(rel_date(9, 0, 0));
        hard_reset(false, 2);
        static const char GET2[] = "\x02{\"cmd\": \"cfg_get_config\"}\x03";
        logs_clear();
        serial_inject(GET2, sizeof(GET2) - 1);
        GUARDED(serialEvent());
        const bool framed = out_has_frame('W');
        char frame[110];
        snprintf(frame, sizeof(frame), "\x02{\"cmd\": \"cfg_set_date_time\", \"device_date_time\": \"%04u-%02u-%02u 15:04:05\"}\x03",
                 (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
        logs_clear(); buzz_clear();
        serial_inject(frame, strlen(frame));
        GUARDED(serialEvent());
        const DateTime now = rtc.GetCurrentDateTime();
        tlog("  설정기 꼴: 읽기 응답=%d 시각 %02u:%02u:%02u updated=%d\n", framed, now.hour(), now.minute(), now.second(), lcd_has("updated"));
        CHECK(framed && now.hour() == 15 && now.minute() == 4 && now.second() >= 5 && now.second() <= 9 && lcd_has("updated"),
              "④b(19차) 설정기 꼴(구분자에 빈칸)의 설정 읽기·시각 맞춤도 같은 계약으로 받는다");
    }

    // ⑤ 한 버퍼에 시각+설정 읽기가 붙어 오면 둘째는 버린다 — 업로더가 둘 사이를 2.5초 띄우는 이유(이 규칙이 바뀌면 그 대기도 다시 본다)
    {
        power_restore(); deviceOption.SetType('W'); hard_reset(false, 2);
        char js[96];
        char both[160];
        rel_json_set_time(js, sizeof(js), 15, 0, 0);
        snprintf(both, sizeof(both), "\x02%s\x03%s", js, GET);
        logs_clear(); buzz_clear();
        serial_inject(both, strlen(both));
        GUARDED(serialEvent());
        tlog("  ⑤ 붙어 온 둘: 시각=%02u:%02u 응답프레임=%d\n", rtc.GetCurrentDateTime().hour(), rtc.GetCurrentDateTime().minute(),
             strchr(g_serialOut, 0x02) != nullptr);
        CHECK(rtc.GetCurrentDateTime().hour() == 15 && strchr(g_serialOut, 0x02) == nullptr,
              "⑤ 사실: 한 버퍼의 둘째 JSON 은 버린다(첫째인 시각만 처리 · 응답 없음) — 업로더는 명령을 띄워 보낸다");
    }
    done();
    for (;;) {}
}
