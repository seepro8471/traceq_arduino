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

// 펌웨어 빌드 도장(uint32) 저장 주소 — 옵션 영역(0~176) 밖.
// 저장된 도장 ≠ 현재 빌드 도장이면 "새로 업로드된 펌웨어의 첫 부팅"으로
// 판단해 EEPROM 전체(설정값 포함)를 소거하고 기본값을 기록한다 (2.2.3,
// 사용자 확정). 1.0의 DATA_NEEDS_INIT(4095) 방식은 구버전 프로그램이
// 깔려 있던 기기에서 플래그 자리에 우연히 0이 있으면 초기화를 건너뛰어,
// "초기화 전용 빌드를 한 번 올렸다가 다시 올리는" 현장 이중 작업이
// 필요했다 — 빌드 도장 방식은 어떤 이전 상태에서도 확실히 1회 초기화된다.
constexpr uint16_t FIRMWARE_STAMP_ADDR{4088};

// 컴파일 시각 FNV-1a 해시 — 빌드마다 유일한 도장.
static uint32_t firmware_build_stamp()
{
    const char *s = __DATE__ " " __TIME__;
    uint32_t h = 2166136261UL;
    while (*s)
    {
        h ^= static_cast<uint8_t>(*s++);
        h *= 16777619UL;
    }
    return h;
}

// 액교환일 기본값 = 현재로부터 1개월 전 (2.2.4, 사용자 확정).
// 초기화 직후 액교환일이 비어 있으면 소독 기록마다 현재시각이 교환일로
// 찍혀 통계 주기가 기록 건건이 흩어진다 — 클리어 태그로 실제 교환을
// 등록하기 전까지 안정된 기준일을 제공한다. (말일이 짧은 달로 넘어가면
// 그 달의 말일로 보정: 3/31 → 2/28)
static LocalDateTime default_clear_datetime(DefaultRtc &rtcRef)
{
    LocalDateTime t = rtcRef.GetCurrentLocalDateTime();
    uint16_t year = t.Date.Year;
    uint8_t month = t.Date.Month;
    if (month <= 1)
    {
        month = 12;
        year -= 1;
    }
    else
    {
        month -= 1;
    }
    static const uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint8_t maxDay = (month >= 1 && month <= 12) ? kDays[month - 1] : 28;
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
    {
        maxDay = 29;
    }
    if (t.Date.Day > maxDay) t.Date.Day = maxDay;
    t.Date.Year  = year;
    t.Date.Month = month;
    return t;
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

void handle_menu_recursive(UserInterface::MenuFunction function);

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
    const uint32_t currentStamp = firmware_build_stamp();
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
        // 기록해 두면 나중에 D 타입으로 바꿔도 유효).
        disinfectionOption.SetClearDateTime(default_clear_datetime(rtc));
        EEPROM.put(FIRMWARE_STAMP_ADDR, currentStamp);
    }

    deviceType = deviceOption.GetType();
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
    ui.DisplayHome(rtc, deviceOption.GetNumber());

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
        handle_menu_recursive(ui.DisplayMenu());
    }
#endif

    // 태그 폴링 — 새 태그만 처리.
    if (rfid.Poll() != RfidController::TagStatus::Connected) return;

    // Company 검사 (회사 코드 일치 여부).
    Company company{};
    if (rfid.Read(SECTOR0_COMPANY, &company, sizeof(Company)) != RfidResult::Ok ||
        company.CompanyCode != TRACEQ_COMPANY_CODE)
    {
        // 1.0과 동일하게 1회 재시도 (약한 신호 보정).
        delay(100);
        if (rfid.Read(SECTOR0_COMPANY, &company, sizeof(Company)) != RfidResult::Ok ||
            company.CompanyCode != TRACEQ_COMPANY_CODE)
        {
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
            disinfectionOption.SetClearDateTime(rtc.GetCurrentLocalDateTime());
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
    default: abort();
    }
#endif

    // 항상 세션 종료 — HaltA + StopCrypto1 보장.
    rfid.EndSession();
}

// Arduino 코어가 loop() 사이에 수신 데이터가 있으면 호출한다.
__attribute__((unused)) void serialEvent()
{
#ifndef READER_MODE
    // 1.0과 동일한 수신 방식: 통짜 단일 write 전제(현장 PC 검증 완료 형식),
    // 512B가 차거나 바이트 간 1000ms(Stream 기본 타임아웃)까지 블로킹.
    // 단 마지막 1바이트를 남겨(511) 항상 NUL 종료를 보장한다.
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

void handle_menu_recursive(UserInterface::MenuFunction function)
{ // NOLINT(misc-no-recursion)
    util_buzzer();
    ui.ClearScreen();
    delay(500);

    switch (function)
    {
    case UserInterface::MenuFunction::Home: ui.DisplayHome(rtc, deviceOption.GetNumber()); break;
    case UserInterface::MenuFunction::Exit:
    case UserInterface::MenuFunction::Save: break;
    case UserInterface::MenuFunction::Next: handle_menu_recursive(ui.DisplayNextMenu()); break;
    case UserInterface::MenuFunction::Prev: handle_menu_recursive(ui.DisplayPrevMenu()); break;
    default: break;
    }

    switch (function)
    {
    case UserInterface::MenuFunction::Date:   handle_menu_recursive(ui.SetDeviceDate(rtc)); break;
    case UserInterface::MenuFunction::Time:   handle_menu_recursive(ui.SetDeviceTime(rtc)); break;
    case UserInterface::MenuFunction::Type:   handle_menu_recursive(ui.SetDeviceType(deviceOption)); break;
    case UserInterface::MenuFunction::Number: handle_menu_recursive(ui.SetDeviceNumber(deviceOption)); break;
    default: break;
    }

    if (deviceType == WASHING_TYPE_DEVICE || deviceType == DISINFECTION_TYPE_DEVICE)
    {
        switch (function)
        {
        case UserInterface::MenuFunction::AlarmFlag:            handle_menu_recursive(ui.SetRecordAlarmFlag(alarmOption)); break;
        case UserInterface::MenuFunction::AlarmTimeSlot:        handle_menu_recursive(ui.SetRecordAlarmTimeSlot(deviceType, alarmOption)); break;
        case UserInterface::MenuFunction::PatientCheck:         handle_menu_recursive(ui.SetRecordPatientCheck(recordOption)); break;
        case UserInterface::MenuFunction::ManagerDisposability: handle_menu_recursive(ui.SetRecordManagerDisposability(recordOption)); break;
        default: break;
        }
    }
    if (deviceType == DISINFECTION_TYPE_DEVICE)
    {
        switch (function)
        {
        case UserInterface::MenuFunction::MaximumCount: handle_menu_recursive(ui.SetDisinfectionMaximumCount(disinfectionOption)); break;
        case UserInterface::MenuFunction::GroupDelay:   handle_menu_recursive(ui.SetDisinfectionGroupDelay(disinfectionOption)); break;
        case UserInterface::MenuFunction::Range:        handle_menu_recursive(ui.SetDisinfectionRange(disinfectionOption)); break;
        default: break;
        }
    }
    // (2.2.0: 서버 Version(Latest/Old) 메뉴 삭제 — 레거시 단일 경로)
}

#endif
