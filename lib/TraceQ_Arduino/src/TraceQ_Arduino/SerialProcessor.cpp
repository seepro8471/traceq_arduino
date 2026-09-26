#include "SerialProcessor.hpp"

#include "TraceQ_Arduino/avr/AvrString.hpp"

void SerialProcessor::LoopProcess(LcdPrinter &printer)
{
    if (!print_tag_number(printer) || !read_tag_serial() || !read_process())
    {
        printer.CustomWarning(0, 2, 100, 4, F("Read Error"));
        return;
    }

    if (!legacy_loop_process(printer)) return;
    // 덤프 일부가 0 으로 나갔으면 태그를 지우지 않는다 — 재스캔으로 온전한 덤프를 다시 받게.
    if (mLegacyReadFailures != 0) return;

    // 초기화가 실패했는데 완료음을 내면, 이전 환자 정보가 남은 태그가 그대로 다음 검사로 나간다.
    // ★순서: **완료 커밋을 먼저**, 소거를 전부 그 뒤에 (사장님 09-25 판정 — PC 기록을 지키는 쪽).
    //  커밋 전에는 아무것도 안 지워졌으므로, 커밋이 실패해 재접촉하면 2차 덤프가 1차와 **완전히 같다**
    //  → PC 는 중복으로 걸러낸다. 소거를 커밋 앞에 두면 일부만 지워진 상태로 2차 덤프가 나가
    //  PC 에 이미 저장된 행의 그 칸들(검사일시·검사항목·환자)이 빈 값으로 덮어써진다.
    //  Status 를 커밋과 같은 쓰기로 내려 "환자정보 있음인데 블록은 빈" 태그도 생기지 않는다.
    //  대가: 커밋 뒤 소거가 실패하면 태그에 지난 환자정보·검사항목이 남고 재접촉으로는 못 지운다
    //  (다음 검사의 게이트웨이 기록이 덮고, 게이트웨이를 안 쓰면 세척기가 '환자정보 없음' 을 알린다).
    const bool ok = commit_dump_done()
                 && clear_gateway_and_subject()
                 && clear_patient();
    if (!ok)
    {
        printer.CustomWarning(0, 2, 100, 4, F("Write Error"));
        return;
    }

    complete_delay();
    util_buzzer(150);
}

bool SerialProcessor::clear_gateway_and_subject()
{
    // SECTOR1: gateway(5) 는 단건 Clear (인접 블록 6 은 Process 라 건드리지 않는다).
    if (mScanner.Clear(SECTOR1_GATEWAY) != RfidResult::Ok) return false;
    return mScanner.ClearSector(15) == RfidResult::Ok;
}

bool SerialProcessor::commit_dump_done()
{
    // 여기서 세척·소독 표시까지 0 — 이 순간부터 이 태그는 다시 덤프되지 않는다.
    mCachedProcess = Process{};
    return write_process();
}

bool SerialProcessor::clear_patient()
{
    // SECTOR2: patient_key(8), patient_name(9), washing_start(10)는 같은 섹터.
    //         washing_start 까지 지우면 안 되므로 환자 두 블록만.
    uint8_t zero[2 * MIFARE_BLOCK_SIZE]{};
    return mScanner.WriteBlocks(SECTOR2_PATIENT_KEY, 2, zero, MIFARE_BLOCK_SIZE) == RfidResult::Ok;
}

SerialProcessor::ProcessKind SerialProcessor::GetProcessKind(
    const char *buffer, size_t length, LcdPrinter &printer)
{
    if (buffer == nullptr || length == 0) return ProcessKind::NotJson;

    // '{' .. '}' 범위만 추출 — 길이 명시.
    const size_t start = str_index_of(buffer, '{');
    if (start == static_cast<size_t>(-1)) return ProcessKind::NotJson;
    const size_t end = str_index_of_range(buffer, '}', start + 1);
    if (end == static_cast<size_t>(-1)) return ProcessKind::NotJson;
    if (end + 1 > length) return ProcessKind::NotJson;

    const auto err = deserializeJson(mDocument, buffer + start, (end - start) + 1);
    if (err)
    {
        printer.Notify_cstr(0, 2, 1000, err.c_str());
        return ProcessKind::DeserializeError;
    }

    const char *cmd = mDocument["cmd"].as<const char *>();
    if (cmd == nullptr) return ProcessKind::Unknown;

    if (!strcmp(cmd, "cfg_new_tag"))       return ProcessKind::NewTag;
    if (!strcmp(cmd, "cfg_get_config"))    return ProcessKind::GetConfig;
    if (!strcmp(cmd, "cfg_set_config"))    return ProcessKind::SetConfig;
    if (!strcmp(cmd, "cfg_set_date_time")) return ProcessKind::SetDateTime;
    return ProcessKind::Unknown;
}

void SerialProcessor::NewTag(DefaultRtc &rtc, LcdPrinter &printer)
{
    const unsigned long timeout = millis();
    unsigned long interval = timeout;
    bool isHandled = false;

    const int typeId = mDocument["type_id"].as<int>();

    mScanner.ForgetTag();   // 설정 프로그램 안내대로 먼저 올려 둔 태그도 잡는다
    while (millis() - timeout < 4000)
    {
        if (mScanner.Poll(true) == RfidController::TagStatus::Connected)   // 정지된 태그도 깨운다(발급 대기)
        {
            // typeId 0: factory default → TraceQ (KEY_A(FF) 인증 → TraceQ 키 설치).
            //        1: TraceQ 재설정 (데이터만 소거).
            //        2: TraceQ → factory default (데이터 소거 후 공장 키 복원).
            // 1.0(SerialProcessor.cpp:117-139)과 동일 순서:
            //   [0] SetAccessMethod → ClearTag → Company 기록
            //   [1] ClearTag → Company 기록
            //   [2] ClearTag → InitAccessMethod
            if (typeId == 0 && mScanner.InstallTraceQKeys() != RfidResult::Ok) break;

            if (mScanner.ClearTag() != RfidResult::Ok) break;

            if (typeId == 2)
            {
                if (mScanner.RestoreFactoryKeys() != RfidResult::Ok) break;
            }
            else
            {
                Company company{};
                company.CompanyCode    = TRACEQ_COMPANY_CODE;
                company.RegisteredDate = rtc.GetCurrentLocalDateTime().Date;
                if (mScanner.Write(SECTOR0_COMPANY, &company, sizeof(company)) != RfidResult::Ok)
                    break;
            }
            isHandled = true;
            break;
        }
        if (millis() - interval > 1000)
        {
            util_buzzer();
            interval = millis();
        }
    }
    mScanner.EndSession();
    if (isHandled)
        printer.Notify(0, 2, 500, F("tag created"));
    else
        printer.CustomWarning(0, 2, 100, 4, F("timeout or error"));
}

void SerialProcessor::WriteOptionData(
    AlarmOption &alarmOption, DeviceOption &deviceOption,
    DisinfectionOption &disinfectionOption, ManagerOption &managerOption,
    RecordOption &recordOption, DefaultRtc &rtc, LcdPrinter &/*printer*/)
{
    StaticJsonDocument<kFrameBufferSize> doc;

    char type[2]{};
    type[0] = deviceOption.GetType();
    doc["device_type"]   = type;
    doc["device_number"] = deviceOption.GetNumber();

    doc["alarm_sound"]   = alarmOption.GetFlag();
    doc["washing_time"]  = alarmOption.GetTimeSlot1();
    doc["df_time"]       = alarmOption.GetTimeSlot2();

    unsigned char key[ManagerOption::KEY_SIZE]{};
    managerOption.GetKey(key, ManagerOption::KEY_SIZE);
    unsigned char name[ManagerOption::NAME_SIZE]{};
    managerOption.GetName(name, ManagerOption::NAME_SIZE);
    doc["manager_key"]  = key;
    doc["manager_name"] = name;

    char clearDateTime[]{"YYYY-MM-DD hh:mm:ss"};
    doc["df_cnt"]             = disinfectionOption.GetCount();
    doc["df_max_cnt"]         = disinfectionOption.GetMaximumCount();
    doc["df_sim_delay"]       = disinfectionOption.GetSimultaneousDisinfectionDelay();
    doc["df_sim_slot"]        = disinfectionOption.GetSimultaneousDisinfectionSlot();
    doc["df_clear_cnt"]       = disinfectionOption.GetClearCount();
    doc["df_clear_date_time"] = rtc.ToString(disinfectionOption.GetClearDateTime(), clearDateTime);

    doc["patient_check"] = recordOption.GetPatientCheck();

    Serial.print(STX);
    serializeJson(doc, Serial);
    Serial.print(ETX);
}

void SerialProcessor::UpdateOptionData(
    AlarmOption &alarmOption, DeviceOption &deviceOption,
    DisinfectionOption &disinfectionOption, ManagerOption &/*managerOption*/,
    RecordOption &recordOption, DefaultRtc &rtc, LcdPrinter &printer)
{
    update_alarm_option(alarmOption);
    update_record_option(recordOption);
    update_disinfection_option(disinfectionOption);

    if (update_device_option(deviceOption, rtc))
    {
        printer.Notify(0, 2, 1000, F("updated, restart"));
        util_soft_reset(); // 1.0의 nullptr 점프 리셋과 동일 동작 (2.0은 이 호출이 누락돼 재시작이 안 됐음)
    }
    else
    {
        printer.Notify(0, 2, 1000, F("updated"));
    }
}

void SerialProcessor::UpdateDateTime(DefaultRtc &rtc, LcdPrinter &printer)
{
    const char *dateTime = mDocument["device_date_time"].as<const char *>();
    if (dateTime != nullptr)
    {
        rtc.FromString(dateTime);
        printer.Notify(0, 2, 1000, F("updated"));
    }
}

bool SerialProcessor::update_device_option(DeviceOption &deviceOption, DefaultRtc &rtc)
{
    // ★재시작 여부는 **저장된 뒤 실제 값**으로 판정한다. 요청값과 비교하면, 알 수 없는 타입("s" 등)을
    //  받았을 때 SetType 은 기본값 'W' 로 폴백하는데 비교는 계속 어긋나 ① 서버가 세척기가 되고
    //  ② 같은 JSON 을 보낼 때마다 매번 재시작했다.
    const char *type = mDocument["device_type"].as<const char *>();
    bool typeChanged = false;
    if (type != nullptr && type[0] != deviceOption.GetType())
    {
        const char before = deviceOption.GetType();
        deviceOption.SetType(type[0]);
        typeChanged = (deviceOption.GetType() != before);
    }
    if (mDocument.containsKey("device_number"))
    {
        const int number = mDocument["device_number"].as<int>();
        if (number != deviceOption.GetNumber()) deviceOption.SetNumber(number);
    }

    const char *dateTime = mDocument["device_date_time"].as<const char *>();
    if (dateTime != nullptr) rtc.FromString(dateTime);
    return typeChanged;
}

void SerialProcessor::update_alarm_option(AlarmOption &alarmOption)
{
    // ★없는 키는 건드리지 않는다 — 종전엔 빠진 키가 0/false 로 저장돼, 설정 일부만 보내면
    //  세척시간이 0 이 되고 자동 종료가 시작과 같은 초로 태그에 박혔다.
    if (mDocument.containsKey("alarm_sound"))
    {
        const bool alarmSound = mDocument["alarm_sound"].as<bool>();
        if (alarmSound != alarmOption.GetFlag()) alarmOption.SetFlag(alarmSound);
    }

    if (mDocument.containsKey("washing_time"))
    {
        const int washingTime = mDocument["washing_time"].as<int>();
        if (washingTime != alarmOption.GetTimeSlot1()) alarmOption.SetTimeSlot1(washingTime);
    }

    if (mDocument.containsKey("df_time"))
    {
        const int dfTime = mDocument["df_time"].as<int>();
        if (dfTime != alarmOption.GetTimeSlot2()) alarmOption.SetTimeSlot2(dfTime);
    }
}

void SerialProcessor::update_disinfection_option(DisinfectionOption &d)
{
    if (mDocument.containsKey("df_max_cnt"))
    {
        const int maxCnt = mDocument["df_max_cnt"].as<int>();
        if (maxCnt != d.GetMaximumCount()) d.SetMaximumCount(maxCnt);
    }

    // int 그대로 넘긴다 — setter 가 범위를 먼저 자르고 좁힌다(300 이 44 가 되던 것).
    if (mDocument.containsKey("df_sim_delay"))
    {
        const int delay = mDocument["df_sim_delay"].as<int>();
        if (delay != d.GetSimultaneousDisinfectionDelay()) d.SetSimultaneousDisinfectionDelay(delay);
    }

    if (mDocument.containsKey("df_sim_slot"))
    {
        const int slot = mDocument["df_sim_slot"].as<int>();
        if (slot != d.GetSimultaneousDisinfectionSlot()) d.SetSimultaneousDisinfectionSlot(slot);
    }

    if (mDocument.containsKey("df_clear_cnt"))
    {
        const int clearCnt = mDocument["df_clear_cnt"].as<int>();
        if (clearCnt != d.GetClearCount()) d.SetClearCount(clearCnt);
    }
}

void SerialProcessor::update_record_option(RecordOption &recordOption)
{
    if (mDocument.containsKey("patient_check"))
    {
        const bool patientCheck = mDocument["patient_check"].as<bool>();
        if (patientCheck != recordOption.GetPatientCheck())
            recordOption.SetPatientCheck(patientCheck);
    }
}

// ─────────────────────────────────────────────────────────────────────────
// legacy 프로토콜 — 1.0 SerialProcessor_legacy.cpp 전체 이식.
// 구 TraceQ Desktop(레거시 서버 모드)과의 와이어 계약이므로 출력 바이트를
// 바꾸면 안 된다: 'Z' 인증, C/M/S 태그 발급, PSOk/Z 핸드셰이크,
// "TTBB{32자hex};" 블록 라인, B; C; S; G; W; Ok! 순서.
// ─────────────────────────────────────────────────────────────────────────

void SerialProcessor::LegacySerialEvent(const char *buffer, size_t length, LcdPrinter &printer)
{
    if (buffer == nullptr || length == 0) return;

    if (!mIsAuthenticated)
    {
        // 1.0과 동일: 버퍼에 'Z'가 포함되면 인증 성립.
        if (str_contains(buffer, 'Z'))
        {
            printer.Notify(0, 2, 500, F("Program Start"));
            mIsAuthenticated = true;
        }
        return;
    }

    switch (buffer[0])
    {
    case 'C':
    case 'M':
    case 'S':
        legacy_create_tag(buffer, printer);
        break;
    default:
        break;
    }
}

bool SerialProcessor::legacy_loop_process(LcdPrinter &printer)
{
    // 서버에서 필요한 데이터가 제대로 기록되어 있는지 검사한다.
    if (mCachedProcess.WashingStatus == 0 && mCachedProcess.DisinfectionCount == 0)
    {
        printer.RejectDebug(0, 2, F("Not W and D"));
        return false;
    }
    if (mCachedProcess.WashingStatus == 0)
    {
        printer.RejectDebug(0, 2, F("Not Washing"));
        return false;
    }
    if (mCachedProcess.DisinfectionCount == 0)
    {
        printer.RejectDebug(0, 2, F("Not Disinfection"));
        return false;
    }

    // 연결 상태 검사 (PSOk → 'Z' 핸드셰이크).
    if (!legacy_is_connected())
    {
        printer.RejectDebug(0, 2, F("Not Connected"));
        return false;
    }

    mLegacyReadFailures = 0;

    // DisinfectionRecord
    legacy_print_sector(5);
    legacy_print_sector(6);
    if (mCachedProcess.DisinfectionCount == 2)
    {
        legacy_print_sector(7);
        legacy_print_sector(8);
    }

    // DisinfectionDetail
    legacy_print_sector(14);

    // 이전 소독기 정보 (deprecated — 구버전이 출력하므로 Clear 후 0 덤프 유지).
    mScanner.Clear(SECTOR4_DISINFECTION);
    Serial.println(F("B;"));
    legacy_print_block(19, SECTOR4_DISINFECTION);

    // company
    Serial.println(F("C;"));
    legacy_print_block(3, SECTOR0_COMPANY);

    // scope
    Serial.println(F("S;"));
    legacy_print_block(3, SECTOR0_TAG);
    legacy_print_block(7, SECTOR1_TAG_SERIAL);

    // gateway
    Serial.println(F("G;"));
    legacy_print_block(7, SECTOR1_GATEWAY);
    legacy_print_block(7, SECTOR1_PROCESS);

    legacy_print_block(11, SECTOR2_PATIENT_KEY);
    legacy_print_block(11, SECTOR2_PATIENT_NAME);

    legacy_print_sector(15);

    // 더 이상 사용하지 않으나 구버전에서 출력하였음.
    mScanner.Clear(52);
    legacy_print_block(55, 52);

    // washing
    Serial.println(F("W;"));
    legacy_print_block(11, SECTOR2_WASHING_START);
    legacy_print_sector(3);
    legacy_print_block(19, SECTOR4_WASHING_END_MANAGER_KEY);
    legacy_print_block(19, SECTOR4_WASHING_END_MANAGER_NAME);

    // 전송 완료
    Serial.println(F("Ok!"));

    if (mLegacyReadFailures != 0)
        printer.CustomWarning(0, 2, 100, 4, F("Read Error"));

    return true;
}

void SerialProcessor::legacy_create_tag(const char *buffer, LcdPrinter &printer)
{
    // cachedTag, cachedTagSerial 초기화.
    mCachedTag.Number = 0;
    memset(mCachedTag.ID, 0, sizeof(mCachedTag.ID));
    memset(mCachedTagSerial.Serial, 0, sizeof(mCachedTagSerial.Serial));

    const unsigned long timeout{millis()};
    unsigned long interval{timeout};
    bool isHandled{false};

    // 4초 동안 태그 대기 (1초 간격 비프). 먼저 올려 둔 태그도 잡는다.
    mScanner.ForgetTag();
    while (millis() - timeout < 4000)
    {
        if (mScanner.Poll(true) == RfidController::TagStatus::Connected)   // 정지된 태그도 깨운다(발급 대기)
        {
            const int parsedType{legacy_parse_tag(buffer)};
            if (parsedType == -1) break;

            // 태그 타입만 교체해 Company 재기록.
            Company company{};
            if (mScanner.Read(SECTOR0_COMPANY, &company, sizeof(company)) != RfidResult::Ok) break;
            company.TagType = parsedType;

            if (mScanner.Write(SECTOR0_COMPANY, &company, sizeof(company)) != RfidResult::Ok) break;
            if (mScanner.Write(SECTOR0_TAG, &mCachedTag, 16) != RfidResult::Ok) break;
            if (mScanner.Write(SECTOR1_TAG_SERIAL, &mCachedTagSerial, 16) != RfidResult::Ok) break;

            // 공정/환자 정보 소거 — ★실패를 무시하면 안 된다. 갓 발급한 스코프가 **지난 환자와 지난
            // 완료 상태(W/D)를 품은 채** "new tag" 로 안내되고, 그 태그를 서버에 대면 어제 날짜
            // 세척·소독 기록이 그대로 저장된다(구형 델파이·세척관리 둘 다 받는다).
            if (mScanner.Clear(SECTOR1_PROCESS) != RfidResult::Ok) break;
            if (mScanner.Clear(SECTOR2_PATIENT_KEY) != RfidResult::Ok) break;
            if (mScanner.Clear(SECTOR2_PATIENT_NAME) != RfidResult::Ok) break;

            isHandled = true;
            break;
        }
        if (millis() - interval > 1000)
        {
            util_buzzer();
            interval = millis();
        }
    }
    mScanner.EndSession();

    if (isHandled)
    {
        auto uiString{F("new tag : clear")};
        if (buffer[0] == 'M') uiString = F("new tag : manager");
        if (buffer[0] == 'S') uiString = F("new tag : scope");
        printer.Notify(0, 2, 500, uiString);
    }
    else
    {
        printer.CustomWarning(0, 2, 100, 4, F("timeout or error"));
    }
}

bool SerialProcessor::legacy_is_connected()
{
    Serial.println(F("PSOk"));
    for (uint8_t i = 0; i < 55; ++i)
    {
        delay(10);
        if (Serial.available() > 0 && Serial.read() == 'Z')
        {
            return true;
        }
    }
    return false;
}

void SerialProcessor::legacy_print_sector(const uint8_t sector)
{
    uint8_t numberOfBlock{4};
    uint8_t firstBlock = sector * numberOfBlock;
    const uint8_t sectorTrailer = 4 * sector + 3;
    if (sector == 0)
    {
        numberOfBlock -= 1;
        firstBlock += 1;
    }
    const int offset{numberOfBlock - 2};
    for (int b = 0; b <= offset; ++b)
    {
        legacy_print_block(sectorTrailer, firstBlock + b);
    }
}

void SerialProcessor::legacy_print_block(const uint8_t sectorTrailer, const uint8_t block)
{
    // 읽기 실패 시 0으로 덤프 (1.0과 동일 — 라인 자체는 항상 출력).
    // ★와이어 포맷은 계약이라 바꾸지 않되, 실패 사실은 기억해 두었다가
    //  전송 후 LCD 로 알린다. 그러지 않으면 "정상적으로 보이는 0 데이터"가
    //  서버에 저장되고 아무 흔적도 남지 않는다 (2.2.5).
    memset(mLegacyBuffer, 0, MIFARE_BLOCK_SIZE);
    if (mScanner.Read(block, mLegacyBuffer, MIFARE_BLOCK_SIZE) != RfidResult::Ok)
        ++mLegacyReadFailures;

    Serial.print(sectorTrailer < 0x10 ? "0" : "");
    Serial.print(sectorTrailer, HEX);
    Serial.print(block < 0x10 ? "0" : "");
    Serial.print(block, HEX);
    for (uint8_t index = 0; index < MIFARE_BLOCK_SIZE; index++)
    {
        if (mLegacyBuffer[index] < 0x10) Serial.print(F("0"));
        Serial.print(mLegacyBuffer[index], HEX);
    }
    Serial.println(';');
}

int SerialProcessor::legacy_parse_tag(const char *buffer)
{
    switch (buffer[0])
    {
    case 'C': return CLEAR_TYPE_TAG;
    case 'M': return legacy_parse_manager_tag(buffer);
    case 'S': return legacy_parse_scope_tag(buffer);
    default:  return -1;
    }
}

int SerialProcessor::legacy_parse_manager_tag(const char *buffer)
{
    char string[16]{};
    // tag id: "M<key>;<name>;"의 첫 ';'까지.
    const auto idx = static_cast<int>(str_index_of(buffer, ';'));
    if (idx == -1) return -1;
    str_substring_safe(buffer, string, 16, 1, idx);
    memcpy(mCachedTag.ID, string, sizeof(mCachedTag.ID));

    // name: 두 번째 ';'까지.
    memset(string, 0, sizeof(string));
    const auto idx2 = static_cast<int>(str_index_of_range(buffer, ';', idx + 1));
    if (idx2 == -1) return -1;
    str_substring_safe(buffer, string, 16, idx + 1, idx2);
    memcpy(mCachedTagSerial.Serial, string, sizeof(mCachedTagSerial.Serial));

    return MANAGER_TYPE_TAG;
}

int SerialProcessor::legacy_parse_scope_tag(const char *buffer)
{
    char string[16]{};
    // number: "S<번호>;<시리얼>;"의 첫 ';'까지.
    const auto idx = static_cast<int>(str_index_of(buffer, ';'));
    if (idx == -1) return -1;
    str_substring_safe(buffer, string, 16, 1, idx);
    mCachedTag.Number = str_atoi(string);

    // serial: 두 번째 ';'까지.
    memset(string, 0, sizeof(string));
    const auto idx2 = static_cast<int>(str_index_of_range(buffer, ';', idx + 1));
    if (idx2 == -1) return -1;
    str_substring_safe(buffer, string, 16, idx + 1, idx2);
    memcpy(mCachedTagSerial.Serial, string, sizeof(mCachedTagSerial.Serial));

    return SCOPE_TYPE_TAG;
}
