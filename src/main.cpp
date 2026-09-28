// TraceQ Arduino 2.x — 메인.
//
// 1.0(=1.4.1) main.cpp의 동작 흐름을 유지하되, 다음 변경 사항을 반영:
//   * 매 루프 Rc522Initialize() 재호출 제거 → IsAlive() 실패 시에만 Reinitialize().
//   * serialEvent는 1.0과 동일하게 raw 수신(readBytes)을 유지한다 — 현장 PC
//     (SeePro/TraceQ_Python/구 TraceQ Desktop)는 'Z'/'G1..G5'/C·M·S를 STX 없이
//     raw cp949로 보내기 때문. 단 511바이트 상한으로 NUL 종료를 보장해
//     1.0의 512B 만재 시 OOB 읽기를 수정. (JSON은 버퍼 안의 '{'..'}'만
//     추출하므로 STX 프레이밍 유무와 무관하게 함께 동작.)
//   * RFID는 RfidController로 일원화. 섹터-캐싱 인증 + StopCrypto1 보장.
//   * 소독기(D)의 매니저 태그 저장을 disinfectionProcessor로 정정 — 1.0은
//     washingProcessor에 저장해 M-Check=Yes 소독기가 시작을 항상 거부하는
//     인스턴스 불일치 버그(1.0 main.cpp:335-338)가 있었다.

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <EEPROM.h>

#include "TraceQ_Arduino.hpp"

// 펌웨어 도장(uint32) 저장 주소 — 옵션 영역(0~177) 밖.
// 저장된 도장 ≠ 현재 펌웨어 도장이면 "새 펌웨어의 첫 부팅"으로 판단한다 (2.2.3, 사용자 확정) —
// 공장 초기·손상이면 EEPROM 전체(설정값 포함)를 소거하고 기본값을 기록하고, **쓰던 기기면 지울지 묻는다**
// (2.2.9~ · 10초 무응답 = 유지).
// 1.0의 DATA_NEEDS_INIT(4095) 방식은 구버전이 깔려 있던 기기에서 플래그
// 자리에 우연히 0이 있으면 초기화를 건너뛰어 "초기화 전용 빌드를 한 번
// 올렸다가 다시 올리는" 이중 작업이 필요했다 — 도장 방식은 어떤 이전
// 상태에서도 새 판의 첫 부팅을 확실히 알아챈다(공장·손상은 초기화 · 쓰던 기기는 묻는다).
//
// ★도장의 재료는 **버전 문자열**이다(2.2.5 정정). 처음엔 `__DATE__ __TIME__`
//  을 썼는데, 그것은 "main.cpp 를 다시 컴파일한 시각"이라 lib 의 .cpp 만
//  고친 빌드에서는 값이 그대로여서 초기화가 돌지 않고, 반대로 무관한 헤더를
//  건드리면 초기화가 도는 비결정적 규칙이었다. 버전 기준이면 규칙이 명확하다:
//  **버전을 올린 펌웨어를 올리면 첫 부팅에 초기화(쓰던 기기면 묻는다), 같은 버전 재업로드는 설정 유지.**
constexpr uint16_t FIRMWARE_STAMP_ADDR{4088};

static uint32_t firmware_stamp()
{
    const char *s = TRACEQ_VERSION_STRING;
    uint32_t h = 2166136261UL;   // FNV-1a
    while (*s)
    {
        h ^= static_cast<uint8_t>(*s++);
        h *= 16777619UL;
    }
    return h;
}

// 설정된 기기 타입.
char deviceType{};

// NVM
AlarmOption        alarmOption{};
DeviceOption       deviceOption{};
DisinfectionOption disinfectionOption{};
ManagerOption      managerOption{};
RecordOption       recordOption{};

// 모듈
DefaultRtc      rtc{};
RfidController  rfid{};
DisplayClass    lcd{0x27, 20, 4};
UserInterfaceClass ui{lcd};

#ifndef READER_MODE

constexpr uint16_t BUFFER_SIZE{SerialProcessor::kFrameBufferSize};

// 각 Processor가 공유할 전역 캐시.
Tag       cachedTag{};
TagSerial cachedTagSerial{};
Process   cachedProcess{};

DisinfectionProcessor disinfectionProcessor{cachedTag, cachedTagSerial, cachedProcess, rfid};
GatewayProcessor      gatewayProcessor     {cachedTag, cachedTagSerial, cachedProcess, rfid};
SerialProcessor       serialProcessor      {cachedTag, cachedTagSerial, cachedProcess, rfid};
WashingProcessor      washingProcessor     {cachedTag, cachedTagSerial, cachedProcess, rfid};

void handle_menu(UserInterface::MenuFunction function);

#else

SimpleScanner simpleScanner{rfid};

#endif

// 전원을 켤 때 RIGHT 을 누르고 있으면 같은 판이어도 다시 고를 수 있다 — 유지를 고른 뒤 되돌릴 유일한 길.
// 부팅 전용 둘은 인라인 금지 — main 의 상주 프레임(스택)에 안 얹히게(4차 F).
static bool __attribute__((noinline)) right_held_at_boot()
{
    for (uint8_t i = 0; i < 4; ++i)
    {
        if (digitalRead(PIN_RIGHT_BUTTON) != LOW) return false;
        delay(15);
    }
    return true;
}

// 새 판 첫 부팅(또는 켤 때 RIGHT)에 쓰던 기기면 — 설정을 지울지 묻는다. 조작은 리더기 메뉴와 같다: `<` `>` 로 고르고 MENU 로 확정.
// 10초 무응답이면 유지(안전한 쪽). 1.4.1 과 EEPROM 주소가 같아 유지하면 기기번호·세척시간·담당자·
// 소독 횟수·액교환일이 그대로 남는다.
static bool __attribute__((noinline)) ask_erase_settings()
{
    lcd.init();
    lcd.backlight();
    ui.Info(0, 0, F("Keep settings?      "));

    bool erase = false;          // 기본은 유지
    bool drawn = false;          // 화면에 그려진 선택
    bool first = true;           // 첫 바퀴는 무조건 그린다(우연히 같아도 그려야 한다)
    uint8_t shownSec = 0xFF;
    const unsigned long start = millis();
    // 켤 때 눌려 있던 버튼은 한 번 뗄 때까지 무시 — 누른 채로 켜자마자 골라지는 사고를 막는다.
    bool armed = false;

    while (millis() - start < 10000UL)
    {
        const bool sel = digitalRead(PIN_SELECT_BUTTON) == LOW;
        const bool lft = digitalRead(PIN_LEFT_BUTTON)   == LOW;
        const bool rgt = digitalRead(PIN_RIGHT_BUTTON)  == LOW;

        if (!armed)
        {
            if (!sel && !lft && !rgt) armed = true;
        }
        else if (sel)
        {
            util_buzzer();
            // 손을 뗄 때까지 기다린다 — 안 그러면 첫 loop() 이 눌린 채로 보고 설정 메뉴로 들어간다.
            // 버튼이 붙은 채 고장나도 부팅이 멈추지 않게 2초까지만.
            for (uint8_t i = 0; i < 100 && digitalRead(PIN_SELECT_BUTTON) == LOW; ++i) delay(20);
            return erase;
        }
        else if (lft != rgt)     // 둘이 같은 표본에 읽히면 무시 — 종전엔 초기화 쪽이 골라졌다
        {
            erase = rgt;         // 왼쪽=유지, 오른쪽=완전 초기화 (확정은 MENU)
            util_buzzer(30);
        }

        if (first || drawn != erase)
        {
            first = false;
            drawn = erase;
            ui.Info(0, 1, erase ? F("  Keep settings     ") : F("> Keep settings     "));
            ui.Info(0, 2, erase ? F("> Erase all        ") : F("  Erase all        "));
        }
        const uint8_t left = static_cast<uint8_t>(10 - (millis() - start) / 1000);
        if (left != shownSec)
        {
            shownSec = left;
            char buf[21]{};
            snprintf_P(buf, sizeof(buf), PSTR("<>move  MENU=OK %2us"), left);   // 서식은 플래시에(RAM −20B)
            ui.Info_cstr(0, 3, buf);
        }
        delay(50);
    }
    return false;                // 무응답 = 유지
}

void setup()
{
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_LED, OUTPUT);
    pinMode(PIN_SELECT_BUTTON, INPUT);
    pinMode(PIN_LEFT_BUTTON,   INPUT);
    pinMode(PIN_RIGHT_BUTTON,  INPUT);

    Serial.begin(115200);
    Serial.flush();

    SPI.begin();
    Wire.begin();

    rtc.RtcInitialize();
    rfid.Initialize();

    // 177번지(액교환일 미룸)의 **알 수 없는 값**(3~255)은 **도장 블록보다 먼저** 정리한다 — 뒤에 두면 판 바꿈 유지
    //  갈래의 `!HasPendingClear()` 가 손상값(!=0)을 '미룸 있음' 으로 읽어 교환일이 빈 채 굳고 스스로 낫지 않았다
    //  (v2.2.15 에 내가 뒤로 옮겼다 · 14차 II-A P3-1). 같은 판 재부팅에서도 손상값이면 다음 loop 의 ApplyPendingClear
    //  가 교환일을 '지금' 으로 덮었다(5차 V3). 정상 미룸(1·2)은 시계를 아직 못 맞춘 기기라 그대로.
    if (disinfectionOption.GetClearPending() > DisinfectionOption::kPendingDefault)
        disinfectionOption.SetClearPending(DisinfectionOption::kPendingNone);

    // 새 판(버전)의 첫 부팅 또는 켤 때 RIGHT — 쓰던 기기면 설정을 지울지 묻고(10초 무응답 = 유지), 공장 초기·손상이면
    //  묻지 않고 초기화한다.
    uint32_t storedStamp{};
    EEPROM.get(FIRMWARE_STAMP_ADDR, storedStamp);
    const uint32_t currentStamp = firmware_stamp();
    if (storedStamp != currentStamp || right_held_at_boot())
    {
        if (!deviceOption.HasStoredSettings() || ask_erase_settings())
        {
            // update()는 이미 같은 값인 셀을 건너뛰므로 재초기화 시 빠르고 수명 소모가 적다.
            // 공장(0xFF) 기기는 4KB×3.3ms ≈ 14초가 걸린다(한 번 소거된 기기는 0 이 아닌 칸만 써서 1초 안) —
            //  진행을 보여 주지 않으면 고장처럼 보인다(4차 D).
            lcd.init();
            lcd.backlight();
            ui.Info(0, 0, F("Initializing...     "));
            // ★도장을 먼저 무효로 — 소거 중 리셋이면 도장이 그대로라 다음 부팅이 이 블록을 건너뛰고
            //  반쯤 지워진 설정(타입 W·번호 0)으로 기동했다(Y2 P3-4).
            EEPROM.put(FIRMWARE_STAMP_ADDR, static_cast<uint32_t>(0));
            const int len = EEPROM.length();
            for (int i = 0; i < len; ++i)
            {
                EEPROM.update(i, 0);
                if ((i & 0xFF) == 0)
                {
                    char pct[8]{};
                    snprintf_P(pct, sizeof(pct), PSTR("%3d%%"), static_cast<int>(100L * i / len));
                    ui.Info_cstr(0, 1, pct);
                }
            }
            ui.Info(0, 1, F("100%"));
            alarmOption.Upload();
            disinfectionOption.Upload();
            managerOption.Upload();
            recordOption.Upload();
            // 액교환일 기본값 = 1개월 전 (소독 모드에서 사용 — 타입과 무관하게
            // 기록해 두면 나중에 D 타입으로 바꿔도 유효). 시계가 방전 표지 시각이면 맞춰질 때 정한다.
            // 미룸을 먼저 세운다 — 날짜 도중 끊겨도 다음 부팅이 다시 쓴다(클리어 태그와 같은 규칙).
            // [6차 판정] 아래 "타입을 마지막에" 가 이미 이 갈래의 절단을 전부 재소거로 보내므로 이 한 줄은
            //  **예비 방어**다(단독으로는 어떤 시험도 잠글 수 없다 — AA1 P3-2). 순서 규칙을 한 곳만 다르게
            //  두면 다음 사람이 헷갈리므로 같은 규칙으로 맞춰 둔다.
            disinfectionOption.SetClearPending(DisinfectionOption::kPendingDefault);
            if (!rtc.IsUnsynced())
            {
                disinfectionOption.SetClearDateTime(DisinfectionOption::OneMonthBefore(rtc.GetCurrentLocalDateTime()));
                disinfectionOption.SetClearPending(DisinfectionOption::kPendingNone);
            }
            // ★타입은 기본값 기록 **전체의 맨 마지막** — HasStoredSettings() 가 타입만 보므로, 도중에 끊기면
            //  '설정 없음' 이 되어 다음 부팅이 묻지 않고 스스로 재소거한다. 앞에 두면 반쯤 기록된 기본값이
            //  '유지' 로 굳었다(Z1 P3-3 — 무응답 10초는 유지다).
            deviceOption.Upload();
        }
        else
        {
            // 클리어 태그를 안 쓰던 기기는 교환일 칸이 비어 있어 소독마다 그날이 교환일로 찍혔다 —
            // 완전 초기화와 같이 '1개월 전' 을 넣는다(사장님 09-27). 값이 있으면 그대로.
            if (disinfectionOption.IsClearDateTimeEmpty() && !disinfectionOption.HasPendingClear())
            {
                disinfectionOption.SetClearPending(DisinfectionOption::kPendingDefault);   // 미룸 먼저(위와 같은 규칙)
                if (!rtc.IsUnsynced())
                {
                    disinfectionOption.SetClearDateTime(DisinfectionOption::OneMonthBefore(rtc.GetCurrentLocalDateTime()));
                    disinfectionOption.SetClearPending(DisinfectionOption::kPendingNone);
                }
            }
        }
        EEPROM.put(FIRMWARE_STAMP_ADDR, currentStamp);
    }

    deviceType = deviceOption.GetType();
    // readBytes 는 마지막 바이트 뒤 이만큼 조용해야 끝난다. 올눈(ALLNuN)은 G2~G5 를 250ms 간격으로
    // 따로 보내므로 게이트웨이는 1.4.1 과 같은 1초로 한 버퍼에 받는다(150ms 면 검사일시가 0 이 됐다).
    Serial.setTimeout(deviceType == GATEWAY_TYPE_DEVICE ? 1000 : 150);
    ui.UserInterfaceInitialize(deviceOption.GetType());

#ifndef READER_MODE
    // 서버는 레거시 단일 경로 (2.2.0 에서 Latest 갈래 삭제) — 부팅 시 항상 PSOk.
    if (deviceType == SERVER_TYPE_DEVICE)
        Serial.println(F("PSOk"));
#endif

    util_buzzer();
}

void loop()
{
#ifndef READER_MODE
    // 게이트웨이는 PC 가 준 본체번호를 태그에 기록하므로, 화면에도 **실제로
    // 기록될 번호**를 보여준다(미수신이면 기기 설정값). 2.2.6.
    const int homeNumber = (deviceType == GATEWAY_TYPE_DEVICE)
        ? gatewayProcessor.effective_number(deviceOption.GetNumber())
        : deviceOption.GetNumber();
#else
    const int homeNumber = deviceOption.GetNumber();
#endif
    ui.DisplayHome(rtc, homeNumber);

    // [8차 판정 · 재론 금지] 한 loop 이 태그를 못 보는 시간이 있다 — 알림음(한 접촉 최대 2.6초 = 소독기 MaxCount Over
    //  1초 + 환자 없음 1.6초)·**알람 발화(실측 2430ms = 300ms 펄스 4회 · 13차 HH2 P3-1 정정)**·거부음·메뉴(나올 때까지 ·
    //  화면마다 60초 무조작이면 나간다)·PC 수신 대기. 그 사이 댔다 뗀 태그는 **통째로 없던 일이 된다**(기록은
    //  틀리지 않고 무음이라 사람이 다시 댄다 · CC2 P3-1). 없애려면 폴링을 인터럽트로 옮겨야 해서 두지 않는다.
    // 1.0과 달리 매번 Rc522Initialize() 호출하지 않음. 죽었을 때만 재초기화.
    // [사장님 판정 09-27 · 재론 금지] 리더 표시가 꺼졌다 켜지는 일(= 이 갈래가 도는 일)은 **전원 불안정 외에는
    //  없다**. 그래서 빈도를 세는 표시도 넣지 않는다(사장님 확정). 재초기화 뒤 태그가 다시 잡히는 경우도
    //  현장에선 안 생긴다 — 태그를 올려 두지 않고 접촉하고 바로 뗀다(RfidController::Reinitialize 주석).
    if (rfid.IsAlive())
    {
        ui.InfoReader(true);
    }
    else
    {
        ui.InfoReader(false);
        rfid.Reinitialize();
    }

#ifdef READER_MODE
    // [5차 판정 · 재론 금지] READER_MODE 는 출하 env 에 없고 지금은 컴파일되지 않는다(ReaderUserInterface 에 InfoReader/InfoRow1 없음).
    // 살릴 때 그 둘을 추가할 것.
    ui.Info(0, 2, F("Reader"));
#else
    // 메뉴·PC·게이트웨이로 시계가 맞춰졌으면 미뤄 둔 액교환일을 기록한다(미룸이 있을 때만 시계를 읽는다).
    if (disinfectionOption.HasPendingClear() && !rtc.IsUnsynced())
        disinfectionOption.ApplyPendingClear(rtc.GetCurrentLocalDateTime());

    switch (deviceType)
    {
    case GATEWAY_TYPE_DEVICE:
        ui.InfoRow1(gatewayProcessor.HasPatientInformation()
            ? F("has patient info    ") : F("no patient info     "));
        break;
    case WASHING_TYPE_DEVICE:
    case DISINFECTION_TYPE_DEVICE:
    {
        const auto slot = deviceType == WASHING_TYPE_DEVICE ? 1 : 2;
        rtc.HandleAlarm(slot, alarmOption.GetFlag());
        char alarmBuffer[21]{};   // "120 Min Alarm 119:59" = 20자 + NUL
        const auto alarmTime = slot == 1 ? alarmOption.GetTimeSlot1() : alarmOption.GetTimeSlot2();
        // 남은 시간은 절대 시각 차 — 자정 넘김에서 음수, 60분 초과에서 나머지만 보이던 것(4차 G).
        const int32_t rem = rtc.GetAlarmRemainingSeconds(slot);
        snprintf_P(alarmBuffer, sizeof(alarmBuffer), PSTR("%02d Min Alarm %02ld:%02ld"),
                   alarmTime, static_cast<long>(rem / 60), static_cast<long>(rem % 60));
        for (uint8_t i = static_cast<uint8_t>(strlen(alarmBuffer)); i < 20; ++i) alarmBuffer[i] = ' ';   // 20자 채움 — 자릿수가 줄어도 잔상 없음
        alarmBuffer[20] = 0;
        ui.InfoRow1_cstr(alarmBuffer);
        break;
    }
    case SERVER_TYPE_DEVICE:
    {
        ui.InfoRow1(serialProcessor.IsAuthenticated() ? F("connected           ") : F("not connected       "));
        break;
    }
    default: break;
    }

    if (digitalRead(PIN_SELECT_BUTTON) == LOW)
    {
        util_buzzer();
        delay(500);
        // ★손을 뗄 때까지 기다린다(붙은 채 고장나도 멈추지 않게 2초까지) — 0.6초 넘게 누르면 메뉴 첫 항목(기기번호
        //  편집)이 곧장 선택되던 것(5차 V3). 부팅 선택창(ask_erase_settings)과 같은 규칙.
        for (uint8_t i = 0; i < 100 && digitalRead(PIN_SELECT_BUTTON) == LOW; ++i) delay(20);
        handle_menu(ui.DisplayMenu());
    }
#endif

    // 태그 폴링 — 새 태그만 처리.
    if (rfid.Poll() != RfidController::TagStatus::Connected) return;

    // Company 검사 (회사 코드 일치 여부).
    Company company{};
    RfidResult companyRead = rfid.Read(SECTOR0_COMPANY, &company, sizeof(Company));
    if (companyRead != RfidResult::Ok || company.CompanyCode != TRACEQ_COMPANY_CODE)
    {
        // 1.0과 동일하게 1회 재시도 (약한 신호 보정). [5차 판정 · 재론 금지] 리더 읽기 오류에만 듣고 인증 실패엔
        // 무효(재선택 없이 재인증 불성립) — 100ms 뿐이라 둔다.
        delay(100);
        companyRead = rfid.Read(SECTOR0_COMPANY, &company, sizeof(Company));
        if (companyRead != RfidResult::Ok || company.CompanyCode != TRACEQ_COMPANY_CODE)
        {
            // ★읽기 자체가 실패한 경우에만 사유를 표시한다. 회사코드 불일치(인증은 됐지만 다른 회사의 태그)는
            //  조용히 무시한다. 이 구분이 없어서 "태그를 댔는데 아무 반응이 없다"의 원인이 카드 문제인지 리더
            //  문제인지 알 수 없었다 (2.2.5). ★키가 다른 MIFARE Classic 카드는 인증 실패로 `AuthFailed` 화면·실패음이
            //  난다(t_rfid A2-1 이 잠근 계약 · 1.0 은 읽기 결과를 안 봐 무음이었다). Classic 이 아닌 카드(교통카드 대부분)는
            //  `Poll` 이 `Invalid` 로 걸러 여기까지 안 오고 무음이다.
            if (companyRead != RfidResult::Ok)
            {
                ui.Info_cstr(0, 3, "     ");   // 앞 건의 스코프 번호를 지운다 — print_tag_number 의 형제(14차 II-G P3-3)
                ui.CustomDebug(0, 2, 100, 4, RfidResultName(companyRead));   // 읽기 실패 = 실패음(다시 대면 됨)
            }
            rfid.EndSession();
            return;
        }
    }

#ifdef READER_MODE
    simpleScanner.Scan(company.TagType, ui);
#else
    switch (deviceType)
    {
    case GATEWAY_TYPE_DEVICE:
        if (company.TagType == SCOPE_TYPE_TAG)
        {
            if (gatewayProcessor.HasPatientInformation())
                gatewayProcessor.GatewayProcess(deviceOption.GetNumber(), ui);
            else
                gatewayProcessor.GatewayProcessFallback(deviceOption.GetNumber(), rtc, ui);
        }
        else
        {
            // 번호를 안 읽는 알림은 3행의 앞 건 스코프 번호를 지운다 — print_tag_number 의 형제(15차 III-F · 알림 자리 전부 =
            //  main 6 + 발급 5(SerialProcessor · 16차))
            ui.Info_cstr(0, 3, "     ");
            ui.RejectDebug(0, 2, F("Invalid Tag Type"));   // 여기서 처리할 수 없는 태그 = 거부음
        }
        break;
    case WASHING_TYPE_DEVICE:
        if (company.TagType == SCOPE_TYPE_TAG)
            washingProcessor.WashingProcess(deviceOption.GetNumber(), alarmOption, managerOption, recordOption, rtc, ui);
        else if (company.TagType == MANAGER_TYPE_TAG)
            washingProcessor.SaveManagerData(recordOption, managerOption, ui);
        else
        {
            ui.Info_cstr(0, 3, "     ");
            ui.RejectDebug(0, 2, F("Invalid Tag Type"));   // 여기서 안 되는 태그(클리어 등) — 무음이던 것을 거부음으로 통일(사장님 09-27)
        }
        break;
    case DISINFECTION_TYPE_DEVICE:
        if (company.TagType == SCOPE_TYPE_TAG)
            disinfectionProcessor.DisinfectionProcess(deviceOption.GetNumber(), alarmOption, disinfectionOption,
                                                     managerOption, recordOption, rtc, ui);
        if (company.TagType == MANAGER_TYPE_TAG)
            disinfectionProcessor.SaveManagerData(recordOption, managerOption, ui);
        if (company.TagType != SCOPE_TYPE_TAG && company.TagType != MANAGER_TYPE_TAG && company.TagType != CLEAR_TYPE_TAG)
        {
            ui.Info_cstr(0, 3, "     ");
            ui.RejectDebug(0, 2, F("Invalid Tag Type"));   // 무음이던 것을 거부음으로 통일(사장님 09-27)
        }
        if (company.TagType == CLEAR_TYPE_TAG)
        {
            // ★미룸을 **맨 먼저** 세운다 — 뒤 쓰기 도중 전원이 끊겨도 다음 부팅이 교환일을 다시 쓴다.
            //  교환일 8바이트가 찢기면 섞인 날짜가 소독 기록에 실렸고(Y2 P3-1), 횟수만 써지고 끊기면
            //  액교환이 기록에서 아예 사라졌다(Z1 P3-1 — 횟수 0 이라 MaxCount Over 도 안 떠 다시 댈 이유가 없다).
            disinfectionOption.SetClearPending(DisinfectionOption::kPendingNow);
            disinfectionOption.SetCount(0);
            // 시계가 방전 표지 시각이면 교환일은 시계가 맞춰질 때(첫 소독·메뉴·PC) 기록한다.
            if (!rtc.IsUnsynced())
            {
                disinfectionOption.SetClearDateTime(rtc.GetCurrentLocalDateTime());
                disinfectionOption.SetClearPending(DisinfectionOption::kPendingNone);
            }
            // [15차 사장님께 물음(09-28) · 그대로 · 재론 금지](C4) 더블터치 가드가 없어 두 번 대면 +2 — 소비처는 JSON 통계뿐(나머지는 멱등).
            disinfectionOption.IncrementClearCount();
            disinfectionProcessor.SetMovable();
            ui.Info_cstr(0, 3, "     ");
            ui.Notify(0, 2, 500, F("Clear"));
        }
        break;
    case SERVER_TYPE_DEVICE:
        if (company.TagType == SCOPE_TYPE_TAG)
        {
            if (serialProcessor.IsAuthenticated())
                serialProcessor.LoopProcess(ui);
            else
            {
                ui.Info_cstr(0, 3, "     ");
                ui.Reject(0, 2, F("Not Connected"));   // PC 미인증 — 무음이던 것을 거부음으로(사장님 09-27 통일). 시리얼엔 안 낸다
            }
        }
        else
        {
            ui.Info_cstr(0, 3, "     ");
            ui.RejectDebug(0, 2, F("Invalid Tag Type"));   // 여기서 처리할 수 없는 태그 = 거부음
        }
        break;
    default: util_soft_reset();
    }
#endif

    // 항상 세션 종료 — HaltA + StopCrypto1 보장.
    rfid.EndSession();
}

// Arduino 코어가 loop() 사이에 수신 데이터가 있으면 호출한다.
__attribute__((unused)) void serialEvent()
{
#ifndef READER_MODE
    // 1.0과 동일한 raw 수신: 512B가 차거나 바이트 간 타임아웃(게이트웨이 1초·그 외 150ms, setup)까지
    // 블로킹. 마지막 1바이트를 남겨(511) 항상 NUL 종료를 보장한다.
    char buffer[BUFFER_SIZE]{};
    size_t len = Serial.readBytes(buffer, BUFFER_SIZE - 1);
    if (len == 0) return;

    // ★앞 읽기가 한도(511)에서 끊겼으면 이 버퍼는 그 전문의 꼬리일 수 있다 — 값 한가운데의 'C'/'M'/'S' 가
    //  발급 명령으로 실행돼 리더 위 태그가 덮였다(Z2 P3-2). 단 **버퍼를 버리지는 않는다**: 링이 511 로 차서
    //  뒷동을 ISR 이 이미 버린 경우엔 다음 버퍼가 정당한 새 전문이라, 버리면 환자 패킷을 잃었다(AA2 P1-2).
    //  둘을 구별할 수 없으므로 **서버 레거시 명령과 레거시 시각 동기('T')만** 막는다(게이트웨이는 아래 머리·꼬리 관문이 본다).
    // [6차 판정 · 재론 금지] **한 환자 전문으로는 511 을 만들 수 없다**(실측: SeePro 100B · 세척관리 최대 115B ·
    //  델파이 TraceQ 5조각 167B). 쌓이는 길은 둘뿐인데 사장님 확인(09-27) 으로 둘 다 현장에 없다 — ① 올눈에서 1초 안에
    //  연달아 전송하지 않는다 ② 환자를 보내는 중에 리더 메뉴를 열어 두거나 첫 부팅 소거 중인 일이 없다.
    //  그래서 이 관문은 **보험**이고, 511 초과 횟수를 세는 진단 표시는 넣지 않는다(화면을 건드리지 않는다).
    static bool sTailOfTruncated = false;
    const bool isTail = sTailOfTruncated;

    // ★맨 앞의 'Z'(PC 가 인증·keepalive 로 보내는 한 바이트)는 건너뛰고 명령 머리를 본다 — 세척관리는 연결마다
    //  'Z' 직후 'T…' 를 보내 한 버퍼에 붙는다(델파이는 PSOk 응답에만 'Z' — 333ms 는 그 수신 타이머 주기). 종전엔 머리가 'Z' 라 뒤 명령이 통째로
    //  유실됐다(BB3 P2-1 — 시각 동기가 자동 경로에서 한 번도 실행되지 않았고 발급 명령도 같은 자리다).
    //  인증은 버퍼 전체에서 'Z' 를 찾으므로(LegacySerialEvent) 여기서 건너뛰어도 인증은 그대로 된다.
    const char *cmd = buffer;
    while (*cmd == 'Z') ++cmd;

    // ★이 버퍼가 게이트웨이 전문인가 — '{' 와 G 마커 중 **먼저 오는 쪽**으로 갈래를 정한다.
    //  머리글자만 보면 'Z' 이외의 앞바이트가 붙은 온전한 전문을 통째로 버렸고(CC1 P2-1), 버퍼 어디든 G 마커를
    //  찾으면 값에 ';G2' 가 든 설정 JSON 을 가로챘다(BB1 P3-5). 먼저 오는 쪽 규칙은 둘 다 맞힌다:
    //  검사명에 '{' 가 든 G 패킷은 G 마커가 앞이고, 값에 ';G2' 가 든 JSON 은 '{' 가 앞이다.
    const bool gatewayFrame =
        deviceType == GATEWAY_TYPE_DEVICE && GatewayProcessor::IsGatewayFrame(cmd);   // 'Z' 건너뛴 머리(형제와 같은 규칙 · 16차)

    // ★전문이 덜 왔으면(마지막 머리 뒤에 꼬리가 없으면) 한 조각씩 더 기다려 이어 붙인다. 올눈(ALLNuN · G2~G5 네 조각)과
    //  델파이 TraceQ(G1~G5 다섯 조각)는 조각을 250ms 간격으로 보내는데, 어느 이음매가 1초를 넘으면 종전엔
    //  조각마다 따로 처리돼 환자정보가 **통째로 유실**됐다(BB2 P2-1). ★온전한 전문은 한 번도 더 기다리지 않는다.
    //  ★대기를 짧게(300ms) 낮추자는 안은 **쓰지 않는다**(CC1 P3-0): 막아야 할 이음매가 정의상 1초를 넘는
    //   것이므로(1초 안이면 첫 읽기가 이미 잡았다) 짧추면 이 봉합이 존재하는 이유가 사라진다 — 실제로
    //   1.8초 이음매 잠금이 빨강이 됐다. 폴링 정지(최악 6.2초)는 전문이 덜 왔을 때만이라 감수한다.
    if (gatewayFrame && GatewayProcessor::NeedsMoreBytes(cmd))
    {
        for (uint8_t more = 0; more < 3 && len < BUFFER_SIZE - 1 &&
                               GatewayProcessor::NeedsMoreBytes(cmd); ++more)
        {
            const size_t before = len;
            const size_t add = Serial.readBytes(buffer + len, BUFFER_SIZE - 1 - len);
            if (add == 0) break;
            len += add;
            buffer[len] = 0;   // memmove 뒤 잔재가 새 데이터 바로 뒤에 남는다 — 문자열 끝을 되세운다
            // ★붙인 조각이 **새 레코드 머리**로 시작하면 앞의 미완 레코드를 버린다. 안 버리면 좁히기가 앞
            //  레코드를 잡고 칸마다 다른 레코드를 집어 **두 환자가 섞인 기록**이 성공음과 함께 나갔다(CC1 P1-1).
            //  G2 는 앞부분에 이미 경계 G2 가 있을 때만 새 머리다 — 아니면 'G1 만 늦게 온' 같은 레코드의 G2 라
            //  버리면 본체번호를 잃는다.
            const char *chunk = buffer + before;
            // ★조각 앞의 keepalive 'Z' 는 건너뛰고 본다 — 형제(아래 머리글자 판정 · 발급 머리)는 'Z' 를 건너뛰는데
            //  여기만 날것이라, 'Z' 로 시작하는 새 레코드가 앞 미완 레코드와 섞여 **두 환자가 한 기록**으로 나갔다
            //  (14차 II-A P3-2 · CC1 P1-1 과 같은 부류). 알려진 발신자엔 방아쇠가 없지만 규칙을 형제와 맞춘다.
            while (*chunk == 'Z') ++chunk;
            // G1 도 **그 조각 안에 경계 G2 가 있을 때만** 새 머리다 — 등록번호가 'G1…' 인 환자의 전문이
            //  그 자리에서 갈리면 같은 레코드의 앞부분을 버려 환자를 잃었다(9차 DD1). 대가: G1 조각만 먼저 오고
            //  G2 가 늦는 새 레코드는 본체번호를 잃는다 — 폴백은 **앞서 G1 으로 받은 값이 있으면 그 값**,
            //  없으면(부팅 직후·G1 이 0000) 기기 자체 번호다(effective_number 는 mGateNumber > 0 만 쓴다 ·
            //  10차에 내가 "앞서 받은 값" 으로만 단정한 것을 11차에 다시 정정). 환자를 잃는 쪽보다는 낫다.
            bool newHead = (chunk[0] == 'G' && chunk[1] == '1' && GatewayProcessor::HasMarker(chunk, "G2"));
            if (!newHead && chunk[0] == 'G' && chunk[1] == '2')
            {
                const char saved = buffer[before];
                buffer[before] = 0;
                newHead = GatewayProcessor::HasMarker(buffer, "G2");
                buffer[before] = saved;
            }
            if (newHead)
            {
                const size_t drop = static_cast<size_t>(chunk - buffer);   // 앞 미완 레코드 + 건너뛴 'Z'
                memmove(buffer, chunk, len - drop + 1);
                len -= drop;
                cmd = buffer;   // 버린 앞부분을 가리키던 포인터를 새 머리로
            }
        }
    }

    // 꼬리 표지는 **이어 붙인 뒤** 길이로 정한다 — 붙여서 511 을 채우면 다음 버퍼가 그 전문의 꼬리다(CC1 P3-1).
    sTailOfTruncated = (len == BUFFER_SIZE - 1);

    // raw 명령은 머리로 안다 — 환자명·검사명에 '{…}' 가 있어도 JSON 으로 오판해 버리지 않는다
    // (버리면 다음 스코프에 직전 환자가 기록된다). JSON 은 STX 나 '{' 로 시작하므로 겹치지 않는다.
    // ★게이트웨이 전문은 위 `IsGatewayFrame`('{' 와 G 마커 중 먼저 오는 쪽)으로 가른다 — 머리글자가 아니다.
    //  전문은 G1(델파이 TraceQ·세척관리) 또는 G2(올눈 ALLNuN — G1 을 안 보낸다)로 시작한다.
    // (레거시 시각 동기 'T' 는 '{' 가 없어 어차피 NotJson 으로 오므로 여기 넣지 않는다)
    // [14차 판정 · 재론 금지] 위 "JSON 은 겹치지 않는다" 는 W/D/G 한정이다 — 서버는 `buffer[0]=='Z'` 항 때문에 'Z' 가
    //  앞에 붙은 JSON 을 버린다(II-A P3-3). 세척관리는 JSON 을 안 보내고, 설정기는 PSOk 에 'Z' 로 답하지만 JSON 과
    //  한 버퍼에 겹치는 것은 PSOk 직후 JSON 이 우연히 붙을 때뿐이다(연결 직후 자동 읽기 등 — 그 명령만 무응답,
    //  다시 누르면 된다).
    //  그 항을 지우면 같은 버퍼의 'Z' 인증이 빠지므로 그대로 둔다.
    const bool rawHead =
        gatewayFrame ||
        (deviceType == SERVER_TYPE_DEVICE &&
         (cmd[0] == 'C' || cmd[0] == 'M' || cmd[0] == 'S' || buffer[0] == 'Z'));
    switch (rawHead ? SerialProcessor::ProcessKind::NotJson : serialProcessor.GetProcessKind(buffer, len, ui))
    {
    case SerialProcessor::ProcessKind::NewTag:
        serialProcessor.NewTag(rtc, ui);
        return;
    case SerialProcessor::ProcessKind::GetConfig:
        serialProcessor.WriteOptionData(alarmOption, deviceOption, disinfectionOption,
                                        managerOption, recordOption, rtc, ui);
        return;
    case SerialProcessor::ProcessKind::SetConfig:
        serialProcessor.UpdateOptionData(alarmOption, deviceOption, disinfectionOption,
                                         managerOption, recordOption, rtc, ui);
        return;
    case SerialProcessor::ProcessKind::SetDateTime:
        serialProcessor.UpdateDateTime(rtc, ui);
        return;
    case SerialProcessor::ProcessKind::NotJson:
        if (gatewayFrame)
        {
            // cmd(맨 앞 'Z' 를 건너뛴 포인터)를 넘긴다 — buffer 면 'Z' 뒤의 G1 이 필드 경계가 아니어서
            //  find_marker 가 못 보고 본체번호를 잃었다(9차 DD1 · 세척관리 30초 keepalive 'Z' 가 방아쇠).
            gatewayProcessor.GatewaySerialEvent(cmd, rtc, deviceOption);
            util_buzzer(500);
        }
        // 레거시 시각 동기는 타입과 무관하게 받는다(JSON cfg_set_date_time 과 같은 규칙).
        //  꼬리로는 실행하지 않는다 — 값 한가운데의 'T' 로 시계가 바뀌면 그 뒤 기록 시각이 전부 틀어진다.
        if (cmd[0] == 'T' && !isTail)
            serialProcessor.LegacySetDateTime(cmd, rtc, ui);
        if (deviceType == SERVER_TYPE_DEVICE && !isTail)
            serialProcessor.LegacySerialEvent(buffer, len, ui);
        break;
    default: break;
    }
#endif
}

#ifndef READER_MODE

void handle_menu(UserInterface::MenuFunction function)
{
    // 화면 전환은 반복문으로 — 재귀였을 때는 페이지를 넘길 때마다 스택이 쌓여 약 75회에 전역을 덮었다.
    for (;;)
    {
        util_buzzer();
        ui.ClearScreen();
        ui.InvalidateHome();   // 지운 화면 — 홈 복귀 시 전체 재출력
        delay(500);
        // ★손을 뗄 때까지 기다린다(2초 상한) — 화면 전환의 형제 셋(부팅 선택창·홈→메뉴·**여기**) 중 여기만 빠져,
        //  항목을 0.6초 넘게 누르면 다음 화면의 첫 읽기가 같은 누름을 새 누름으로 먹어 `R S L S` 가 번호 01→11 ·
        //  알람 04→14 로 저장되고 Type 은 재시작까지 했다(5차 V3 P2-1 의 형제 · 14차 II-F P2-1).
        for (uint8_t i = 0; i < 100 && digitalRead(PIN_SELECT_BUTTON) == LOW; ++i) delay(20);

        switch (function)
        {
        case UserInterface::MenuFunction::Home: return;   // 그리지 않는다 — 다음 loop 이 정본 값(게이트웨이 본체번호 포함)으로 그린다
        case UserInterface::MenuFunction::Next:   function = ui.DisplayNextMenu(); continue;
        case UserInterface::MenuFunction::Prev:   function = ui.DisplayPrevMenu(); continue;
        case UserInterface::MenuFunction::Date:   function = ui.SetDeviceDate(rtc); continue;
        case UserInterface::MenuFunction::Time:   function = ui.SetDeviceTime(rtc); continue;
        case UserInterface::MenuFunction::Type:   function = ui.SetDeviceType(deviceOption); continue;
        case UserInterface::MenuFunction::Number: function = ui.SetDeviceNumber(deviceOption); continue;
        default: break;
        }

        if (deviceType == WASHING_TYPE_DEVICE || deviceType == DISINFECTION_TYPE_DEVICE)
        {
            switch (function)
            {
            case UserInterface::MenuFunction::AlarmFlag:            function = ui.SetRecordAlarmFlag(alarmOption); continue;
            case UserInterface::MenuFunction::AlarmTimeSlot:        function = ui.SetRecordAlarmTimeSlot(deviceType, alarmOption); continue;
            case UserInterface::MenuFunction::PatientCheck:         function = ui.SetRecordPatientCheck(recordOption); continue;
            case UserInterface::MenuFunction::ManagerDisposability:
            {
                const bool before = recordOption.GetManagerDisposability();
                function = ui.SetRecordManagerDisposability(recordOption);
                // ★[15차 사장님 A7] 설정이 바뀌면 앞서 세운 일회성 표지를 버린다 — ON→OFF→ON(재부팅 없이) 뒤 담당자 없이 첫 시작이 통과했다.
                if (before != recordOption.GetManagerDisposability())
                {
                    washingProcessor.ResetDisposability();
                    disinfectionProcessor.ResetDisposability();
                }
                continue;
            }
            default: break;
            }
        }
        if (deviceType == DISINFECTION_TYPE_DEVICE)
        {
            switch (function)
            {
            case UserInterface::MenuFunction::MaximumCount: function = ui.SetDisinfectionMaximumCount(disinfectionOption); continue;
            case UserInterface::MenuFunction::GroupDelay:   function = ui.SetDisinfectionGroupDelay(disinfectionOption); continue;
            case UserInterface::MenuFunction::Range:        function = ui.SetDisinfectionRange(disinfectionOption); continue;
            default: break;
            }
        }
        return;   // Exit·Save, 또는 이 타입에 없는 항목
    }
}

#endif
