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

    // 새 빌드의 첫 부팅이면 EEPROM 완전 초기화 (설정값 포함) + 기본값 기록.
    // update()는 이미 같은 값인 셀을 건너뛰므로 재초기화 시 빠르고 수명 소모가 적다.
    uint32_t storedStamp{};
    EEPROM.get(FIRMWARE_STAMP_ADDR, storedStamp);
    const uint32_t currentStamp = firmware_stamp();
    if (storedStamp != currentStamp)
    {
        const int len = EEPROM.length();
        for (int i = 0; i < len; ++i) EEPROM.update(i, 0);
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
        ui.Info(12, 3, F("R-O"));
    }
    else
    {
        ui.Info(12, 3, F("R-X"));
        rfid.Reinitialize();
    }

#ifdef READER_MODE
    ui.Info(0, 2, F("Reader"));
#else
    // 메뉴·PC·게이트웨이로 시계가 맞춰졌으면 미뤄 둔 액교환일을 기록한다.
    if (!rtc.IsUnsynced()) disinfectionOption.ApplyPendingClear(rtc.GetCurrentLocalDateTime());

    switch (deviceType)
    {
    case GATEWAY_TYPE_DEVICE:
        ui.Info(0, 1, gatewayProcessor.HasPatientInformation()
            ? F("has patient info    ") : F("no patient info     "));
        break;
    case WASHING_TYPE_DEVICE:
    case DISINFECTION_TYPE_DEVICE:
    {
        const auto slot = deviceType == WASHING_TYPE_DEVICE ? 1 : 2;
        rtc.HandleAlarm(slot, alarmOption.GetFlag());
        char alarmBuffer[20]{};
        const auto alarmTime = slot == 1 ? alarmOption.GetTimeSlot1() : alarmOption.GetTimeSlot2();
        if (!rtc.HasAlarm(slot))
        {
            snprintf(alarmBuffer, sizeof(alarmBuffer), "%02d Min Alarm %02d:%02d", alarmTime, 0, 0);
        }
        else
        {
            const auto alarmTimeSpan = rtc.GetAlarmTimeSpan(slot);
            const auto ts = alarmTimeSpan - rtc.GetCurrentTimeSpan();
            snprintf(alarmBuffer, sizeof(alarmBuffer), "%02d Min Alarm %02d:%02d",
                     alarmTime, ts.minutes(), ts.seconds());
        }
        ui.Info_cstr(0, 1, alarmBuffer);
        break;
    }
    case SERVER_TYPE_DEVICE:
    {
        ui.Info(0, 1, serialProcessor.IsAuthenticated() ? F("connected           ") : F("not connected       "));
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
                ui.Debug(0, 2, RfidResultName(companyRead));
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
            ui.Debug(0, 2, F("Invalid Tag Type"));
        }
        break;
    case WASHING_TYPE_DEVICE:
        if (company.TagType == SCOPE_TYPE_TAG)
            washingProcessor.WashingProcess(deviceOption.GetNumber(), alarmOption, managerOption, recordOption, rtc, ui);
        if (company.TagType == MANAGER_TYPE_TAG)
            washingProcessor.SaveManagerData(recordOption, managerOption, ui);
        break;
    case DISINFECTION_TYPE_DEVICE:
        if (company.TagType == SCOPE_TYPE_TAG)
            disinfectionProcessor.DisinfectionProcess(deviceOption.GetNumber(), alarmOption, disinfectionOption,
                                                     managerOption, recordOption, rtc, ui);
        if (company.TagType == MANAGER_TYPE_TAG)
            disinfectionProcessor.SaveManagerData(recordOption, managerOption, ui);
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
            ui.Debug(0, 2, F("Invalid Tag Type"));
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

    switch (serialProcessor.GetProcessKind(buffer, len, ui))
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
        case UserInterface::MenuFunction::Home: ui.DisplayHome(rtc, deviceOption.GetNumber()); return;
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
