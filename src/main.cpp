// TraceQ Arduino 2.1 — 메인.
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
// 저장된 도장 ≠ 현재 펌웨어 도장이면 "새 펌웨어의 첫 부팅"으로 판단해
// EEPROM 전체(설정값 포함)를 소거하고 기본값을 기록한다 (2.2.3, 사용자 확정).
// 1.0의 DATA_NEEDS_INIT(4095) 방식은 구버전이 깔려 있던 기기에서 플래그
// 자리에 우연히 0이 있으면 초기화를 건너뛰어 "초기화 전용 빌드를 한 번
// 올렸다가 다시 올리는" 이중 작업이 필요했다 — 도장 방식은 어떤 이전
// 상태에서도 확실히 1회 초기화된다.
//
// ★도장의 재료는 **버전 문자열**이다(2.2.5 정정). 처음엔 `__DATE__ __TIME__`
//  을 썼는데, 그것은 "main.cpp 를 다시 컴파일한 시각"이라 lib 의 .cpp 만
//  고친 빌드에서는 값이 그대로여서 초기화가 돌지 않고, 반대로 무관한 헤더를
//  건드리면 초기화가 도는 비결정적 규칙이었다. 버전 기준이면 규칙이 명확하다:
//  **버전을 올린 펌웨어를 올리면 완전 초기화, 같은 버전 재업로드는 설정 유지.**
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

// 업로드 직후 1회 — 설정을 지울지 묻는다. 조작은 리더기 메뉴와 같다: `<` `>` 로 고르고 MENU 로 확정.
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

    // 새 빌드의 첫 부팅 — 쓰던 기기면 설정을 지울지 묻고, 공장 초기·손상이면 묻지 않고 초기화한다.
    uint32_t storedStamp{};
    EEPROM.get(FIRMWARE_STAMP_ADDR, storedStamp);
    const uint32_t currentStamp = firmware_stamp();
    if (storedStamp != currentStamp || right_held_at_boot())
    {
        if (!deviceOption.HasStoredSettings() || ask_erase_settings())
        {
            // update()는 이미 같은 값인 셀을 건너뛰므로 재초기화 시 빠르고 수명 소모가 적다.
            // 쓰던 기기는 4KB×3.3ms ≈ 14초가 걸린다 — 진행을 보여 주지 않으면 고장처럼 보인다(4차 D).
            lcd.init();
            lcd.backlight();
            ui.Info(0, 0, F("Initializing...     "));
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
            deviceOption.Upload();
            disinfectionOption.Upload();
            managerOption.Upload();
            recordOption.Upload();
            // 액교환일 기본값 = 1개월 전 (소독 모드에서 사용 — 타입과 무관하게
            // 기록해 두면 나중에 D 타입으로 바꿔도 유효). 시계가 방전 표지 시각이면 맞춰질 때 정한다.
            if (rtc.IsUnsynced())
                disinfectionOption.SetClearPending(DisinfectionOption::kPendingDefault);
            else
                disinfectionOption.SetClearDateTime(DisinfectionOption::OneMonthBefore(rtc.GetCurrentLocalDateTime()));
        }
        else
        {
            // 유지 — 177번지(액교환일 미룸)에 **알 수 없는 값**만 정리한다. 옛 판이 안 쓰던 자리라
            // 쓰레기값이면 첫 소독에서 액교환일을 오늘로 덮어쓴다. 정상 미룸(1·2)은 시계를 아직 못 맞춘
            // 기기이므로 그대로 둬야 한다.
            if (disinfectionOption.GetClearPending() > DisinfectionOption::kPendingDefault)
                disinfectionOption.SetClearPending(DisinfectionOption::kPendingNone);
            // 클리어 태그를 안 쓰던 기기는 교환일 칸이 비어 있어 소독마다 그날이 교환일로 찍혔다 —
            // 완전 초기화와 같이 '1개월 전' 을 넣는다(사장님 09-27). 값이 있으면 그대로.
            if (disinfectionOption.IsClearDateTimeEmpty() && !disinfectionOption.HasPendingClear())
            {
                if (rtc.IsUnsynced())
                    disinfectionOption.SetClearPending(DisinfectionOption::kPendingDefault);
                else
                    disinfectionOption.SetClearDateTime(DisinfectionOption::OneMonthBefore(rtc.GetCurrentLocalDateTime()));
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

    // 1.0과 달리 매번 Rc522Initialize() 호출하지 않음. 죽었을 때만 재초기화.
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
        // 1.0과 동일하게 1회 재시도 (약한 신호 보정).
        delay(100);
        companyRead = rfid.Read(SECTOR0_COMPANY, &company, sizeof(Company));
        if (companyRead != RfidResult::Ok || company.CompanyCode != TRACEQ_COMPANY_CODE)
        {
            // ★읽기 자체가 실패한 경우에만 사유를 표시한다. 회사코드 불일치는
            //  "우리 태그가 아님"(호텔 카드 등)이라 1.0처럼 조용히 무시해야
            //  한다. 이 구분이 없어서 "태그를 댔는데 아무 반응이 없다"의 원인이
            //  카드 문제인지 리더 문제인지 알 수 없었다 (2.2.5).
            if (companyRead != RfidResult::Ok)
                ui.CustomDebug(0, 2, 100, 4, RfidResultName(companyRead));   // 읽기 실패 = 실패음(다시 대면 됨)
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
            ui.RejectDebug(0, 2, F("Invalid Tag Type"));   // 여기서 처리할 수 없는 태그 = 거부음
        }
        break;
    case WASHING_TYPE_DEVICE:
        if (company.TagType == SCOPE_TYPE_TAG)
            washingProcessor.WashingProcess(deviceOption.GetNumber(), alarmOption, managerOption, recordOption, rtc, ui);
        else if (company.TagType == MANAGER_TYPE_TAG)
            washingProcessor.SaveManagerData(recordOption, managerOption, ui);
        else
            ui.RejectDebug(0, 2, F("Invalid Tag Type"));   // 여기서 안 되는 태그(클리어 등) — 무음이던 것을 거부음으로 통일(사장님 09-27)
        break;
    case DISINFECTION_TYPE_DEVICE:
        if (company.TagType == SCOPE_TYPE_TAG)
            disinfectionProcessor.DisinfectionProcess(deviceOption.GetNumber(), alarmOption, disinfectionOption,
                                                     managerOption, recordOption, rtc, ui);
        if (company.TagType == MANAGER_TYPE_TAG)
            disinfectionProcessor.SaveManagerData(recordOption, managerOption, ui);
        if (company.TagType != SCOPE_TYPE_TAG && company.TagType != MANAGER_TYPE_TAG && company.TagType != CLEAR_TYPE_TAG)
            ui.RejectDebug(0, 2, F("Invalid Tag Type"));   // 무음이던 것을 거부음으로 통일(사장님 09-27)
        if (company.TagType == CLEAR_TYPE_TAG)
        {
            disinfectionOption.SetCount(0);
            // 시계가 방전 표지 시각이면 교환일은 시계가 맞춰질 때(첫 소독·메뉴·PC) 기록한다.
            if (rtc.IsUnsynced())
            {
                disinfectionOption.SetClearPending(DisinfectionOption::kPendingNow);
            }
            else
            {
                disinfectionOption.SetClearDateTime(rtc.GetCurrentLocalDateTime());
                disinfectionOption.SetClearPending(DisinfectionOption::kPendingNone);
            }
            disinfectionOption.IncrementClearCount();
            disinfectionProcessor.SetMovable();
            ui.Notify(0, 2, 500, F("Clear"));
        }
        break;
    case SERVER_TYPE_DEVICE:
        if (company.TagType == SCOPE_TYPE_TAG)
        {
            if (serialProcessor.IsAuthenticated())
                serialProcessor.LoopProcess(ui);
        }
        else
        {
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
    const size_t len = Serial.readBytes(buffer, BUFFER_SIZE - 1);
    if (len == 0) return;

    // raw 명령(G / C·M·S·Z)은 머리글자로 안다 — 환자명·검사명에 '{…}' 가 있어도 JSON 으로 오판해 버리지 않는다
    // (버리면 다음 스코프에 직전 환자가 기록된다). JSON 은 STX 나 '{' 로 시작하므로 겹치지 않는다.
    const bool rawHead =
        (deviceType == GATEWAY_TYPE_DEVICE && buffer[0] == 'G') ||
        (deviceType == SERVER_TYPE_DEVICE &&
         (buffer[0] == 'C' || buffer[0] == 'M' || buffer[0] == 'S' || buffer[0] == 'Z'));
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
        if (deviceType == GATEWAY_TYPE_DEVICE && buffer[0] == 'G')
        {
            gatewayProcessor.GatewaySerialEvent(buffer, rtc);
            util_buzzer(500);
        }
        if (deviceType == SERVER_TYPE_DEVICE)
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
            case UserInterface::MenuFunction::ManagerDisposability: function = ui.SetRecordManagerDisposability(recordOption); continue;
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
