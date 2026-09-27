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
    //  → 안전망은 **다음 세척 시작의 선행 소거**인데 그것은 **블록 8·9(환자 키·이름)뿐**이다.
    //    ★12차 정정: 블록 5(검사일시·본체번호)와 섹터15(검사항목)는 그 소거가 안 지운다 →
    //    **옛 검사항목이 다음 주기 덤프에 실제로 나간다**(GG1 P3-5 실측). 그 둘의 진짜 안전망은
    //    **다음 검사의 게이트웨이 접촉**이고, 게이트웨이를 안 쓰는 현장에는 안전망이 없다.
    //    세척기의 '환자정보 없음' 경고는 사람에게만 알리고 PC 는 Status 를 안 보고 저장한다(9차 정정).
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

    // [5차 판정 · 재론 금지] 중첩 JSON 은 파싱 실패(2초 안내) · null 값은 0 저장 · 날짜만 온 시각은 00:00 ·
    //  `end+1>length` 는 항상 거짓 — 세 PC 는 어느 것도 보내지 않는다.
    // ★정정(6·7차): *"'Z' 와 명령이 한 버퍼면 명령 유실 — 세 PC 는 안 보낸다"* 는 **틀렸다**. 세척관리는 연결마다
    //  'Z' 직후 'T' 를, 레거시 장치엔 30초마다 'Z' 를, 델파이는 333ms 마다 'Z' 를 보낸다 → 명령 머리를 볼 때
    //  맨 앞 'Z' 를 건너뛰도록 고쳤다(main.cpp serialEvent · LegacySerialEvent).
    // '{' .. '}' 범위만 추출 — 길이 명시.
    const size_t start = str_index_of(buffer, '{');
    if (start == static_cast<size_t>(-1)) return ProcessKind::NotJson;
    // [5차 판정 · 재론 금지] 한 버퍼에 JSON 이 둘이면 둘째는 버린다 — 세 PC 모두 명령을 하나씩 보내고 응답을 기다린다.
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

    // +1 = NUL 자리 — 1.4.1 이 16바이트를 꽉 채워 저장한 기기에서 JSON 직렬화가 스택 밖을 읽었다(5차 E3).
    unsigned char key[ManagerOption::KEY_SIZE + 1]{};
    managerOption.GetKey(key, ManagerOption::KEY_SIZE);
    unsigned char name[ManagerOption::NAME_SIZE + 1]{};
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
    if (type != nullptr && type[0] != 0 && type[0] != deviceOption.GetType())   // "" 는 무시 — 종전엔 W 로 바뀌며 재시작
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

void SerialProcessor::LegacySetDateTime(const char *buffer, DefaultRtc &rtc, LcdPrinter &printer)
{
    if (buffer == nullptr || buffer[0] != 'T') return;
    // 'T' 뒤부터 ';' 로 끊어 7칸. 요일(4번째)은 읽고 버린다 — 두 PC 가 뜻이 다르다.
    int field[7]{};
    size_t at = 1;
    for (uint8_t i = 0; i < 7; ++i)
    {
        const size_t sep = str_index_of_range(buffer, ';', at);
        if (sep == static_cast<size_t>(-1)) { printer.Notify(0, 2, 1000, F("Invalid DateTime")); return; }
        // 칸 경계가 255 를 넘으면 아래 uint8_t 캐스팅이 잘려 **조용히 잘못된 시계**가 된다(BB1 P3-4 실측:
        //  270바이트 전문이 그럴듯한 틀린 시각으로 저장됐다). 우리 시험 표본(300바이트 '9' 채움)은 범위 검사에도
        //  걸리므로 이 한 줄만 지워도 빨강이 안 난다 — 그 절단 경우를 막는 보험이다.
        if (sep > 254) { printer.Notify(0, 2, 1000, F("Invalid DateTime")); return; }
        // str_atoi_range 의 end 는 **포함**이다 — sep(=';')를 넘기면 숫자가 아니라 -1 이 된다.
        // (빈 칸은 begin > end 가 되어 -1 → 아래 범위 검사가 거른다 — 따로 볼 필요가 없다.)
        field[i] = str_atoi_range(buffer, static_cast<uint8_t>(at), static_cast<uint8_t>(sep - 1));
        at = sep + 1;
    }
    const int year = field[0], month = field[1], day = field[2];
    const int hour = field[4], minute = field[5], second = field[6];
    // ★이 범위 검사는 **없으면 안 된다**(6차에 내가 "지워도 구별 못 한다" 고 잘못 적었다 · BB1 P3-1 정정).
    //  비숫자·자리넘침 칸은 `str_atoi_range` 가 -1 을 주고, -1 이 uint16_t 로 가면 RTClib 이 **2047년**으로 저장하며
    //  `isValid()` 는 참이다 — 그 시계는 스스로 낫지 않는다(소독기 복구는 앞으로만 간다).
    if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
    {
        printer.Notify(0, 2, 1000, F("Invalid DateTime"));
        return;
    }
    const DateTime set{static_cast<uint16_t>(year), static_cast<uint8_t>(month), static_cast<uint8_t>(day),
                       static_cast<uint8_t>(hour), static_cast<uint8_t>(minute), static_cast<uint8_t>(second)};
    if (!set.isValid()) { printer.Notify(0, 2, 1000, F("Invalid DateTime")); return; }
    rtc.SetDateTime(set);
    printer.Notify(0, 2, 1000, F("updated"));      // JSON 시각 동기와 같은 안내·소리
}

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

    // 맨 앞의 'Z'(인증·keepalive) 는 건너뛴다 — 위 인증은 버퍼 전체를 보고, 명령은 그 뒤에 붙어 온다(BB3 P2-1).
    const char *cmd = buffer;
    while (*cmd == 'Z') ++cmd;
    switch (cmd[0])
    {
    case 'C':
    case 'M':
    case 'S':
        legacy_create_tag(cmd, printer);
        break;
    default:
        break;
    }
}

// [5차 판정 · 재론 금지] LoopProcess 가 읽은 블록 2·4·6 을 여기서 다시 읽는다(1.0 동일·덤프 함수 자족) — 읽기 3회 ≈ 10ms.
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
    // 소거 실패면 옛 바이트를 읽어 보내지 않는다 — 계약은 "Clear 후 0 덤프" 다.
    const bool b19 = mScanner.Clear(SECTOR4_DISINFECTION) == RfidResult::Ok;
    Serial.println(F("B;"));
    legacy_print_block(19, SECTOR4_DISINFECTION, !b19);

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
    const bool b52 = mScanner.Clear(52) == RfidResult::Ok;
    legacy_print_block(55, 52, !b52);

    // washing
    Serial.println(F("W;"));
    legacy_print_block(11, SECTOR2_WASHING_START);
    legacy_print_sector(3);
    legacy_print_block(19, SECTOR4_WASHING_END_MANAGER_KEY);
    legacy_print_block(19, SECTOR4_WASHING_END_MANAGER_NAME);

    // 전송 완료 — ★읽기 실패가 하나라도 있으면 `Ok!` 를 내지 않는다.
    //  0 으로 나간 블록이 섞인 덤프에도 `Ok!` 를 붙이면 세척관리가 그것을 **저장**하고(저장 방아쇠가 `Ok!` 다),
    //  세척/소독 시작 시각만 온전하면 검증을 통과해 **세척 종료가 빈 행**이 대장에 남았다. 게다가 검증을
    //  통과한 스캔은 5초 중복 차단이 걸려 **곧바로 다시 댄 온전한 스캔이 버려졌다**(CC2 P1-1).
    //  현장에서 태그는 1초 미만으로 댔다 떼므로(사장님 확인) 덤프(카드 동작 65회 + 'Z' 대기)가 중간에
    //  끊기는 것은 드문 일이 아니다. `Ok!` 를 안 내면 두 PC 모두 저장하지 않고, 태그는 그대로 남아
    //  다시 대면 온전한 덤프가 나간다(아래 LoopProcess 의 보존 규칙과 짝을 이룬다).
    //  ★짝이 되는 PC 쪽 조건: **이미 나간 블록은 PC 버퍼에 남는다.** 세척관리는 `PSOk`(덤프 시작 신호)마다
    //   누적을 비워야 한다 — 안 비우면 이 덤프의 2차 소독·검사항목이 **다음 스코프의 저장에 섞인다**
    //   (그 줄들은 조건부 출력이라 다음 덤프가 덮지 못한다 · 9차 DD1·DD3). 리더 쪽으로는 못 닫는다.
    if (mLegacyReadFailures != 0)
    {
        printer.CustomWarning(0, 2, 100, 4, F("Read Error"));
        return true;
    }
    Serial.println(F("Ok!"));

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

    // 파싱이 안 되는 명령은 태그를 기다리지 않는다 — 종전엔 4초 기다린 뒤에야 실패했다.
    const int parsedType{legacy_parse_tag(buffer)};
    if (parsedType == -1)
    {
        printer.CustomWarning(0, 2, 100, 4, F("timeout or error"));
        return;
    }

    // 4초 동안 태그 대기 (1초 간격 비프). 먼저 올려 둔 태그도 잡는다.
    mScanner.ForgetTag();
    while (millis() - timeout < 4000)
    {
        if (mScanner.Poll(true) == RfidController::TagStatus::Connected)   // 정지된 태그도 깨운다(발급 대기)
        {
            // 태그 타입만 교체해 Company 재기록.
            Company company{};
            if (mScanner.Read(SECTOR0_COMPANY, &company, sizeof(company)) != RfidResult::Ok) break;

            // ★종류(표지)를 **0(미지정)으로 먼저 내리고 맨 끝에 세운다** — 중간에 접촉이 끊기면 "종류만 새것 +
            //  번호·ID 는 옛것" 인 카드가 남아 옛 스코프가 담당자로 등록됐다. 미지정은 어느 기기도 거부한다.
            company.TagType = 0;
            if (mScanner.Write(SECTOR0_COMPANY, &company, sizeof(company)) != RfidResult::Ok) break;
            if (mScanner.Write(SECTOR0_TAG, &mCachedTag, 16) != RfidResult::Ok) break;
            if (mScanner.Write(SECTOR1_TAG_SERIAL, &mCachedTagSerial, 16) != RfidResult::Ok) break;

            // 공정/환자 정보 소거 — ★실패를 무시하면 안 된다. 갓 발급한 스코프가 **지난 환자와 지난
            // 완료 상태(W/D)를 품은 채** "new tag" 로 안내되고, 그 태그를 서버에 대면 어제 날짜
            // 세척·소독 기록이 그대로 저장된다(구형 델파이·세척관리 둘 다 받는다).
            if (mScanner.Clear(SECTOR1_PROCESS) != RfidResult::Ok) break;
            if (mScanner.Clear(SECTOR2_PATIENT_KEY) != RfidResult::Ok) break;
            if (mScanner.Clear(SECTOR2_PATIENT_NAME) != RfidResult::Ok) break;
            // 검사일시·본체번호·검사명도 — PC 는 Status 와 무관하게 그 블록을 저장하므로 재발급 태그에 옛 검사가 붙었다(5차 V2)
            if (mScanner.Clear(SECTOR1_GATEWAY) != RfidResult::Ok) break;
            if (mScanner.ClearSector(15) != RfidResult::Ok) break;

            company.TagType = parsedType;   // 표지는 마지막 — 여기까지 와야 이 카드가 그 종류가 된다
            if (mScanner.Write(SECTOR0_COMPANY, &company, sizeof(company)) != RfidResult::Ok) break;

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

// [6차 계약 · 재론 금지] 550ms 는 델파이 `RfidChkTimer` **333ms**(MainFormSo.dfm)가 수신마다 재시작하며
//  'Z' 를 보내는 주기를 견디려는 값이다(여유 217ms) — 350ms 밑으로 줄이면 델파이 현장의 서버 덤프가 전부 죽는다.
bool SerialProcessor::legacy_is_connected()
{
    // PC 는 PSOk 에 'Z' 한 바이트로 답한다(델파이·세척관리·SeePro 셋 다 줄바꿈 없음).
    // ★읽지 않고 들여다본다(peek) — 종전엔 10ms 마다 1바이트씩 **소비**해서, 줄 서 있던 PC 명령
    //  (설정 JSON·발급 명령)을 먹어 버리고, 55바이트를 넘으면 정상 연결인데 'Not Connected' 가 됐다.
    //  'Z' 가 아닌 바이트가 앞에 있으면 그 뒤의 'Z' 에 닿을 수 없으니 곧장 실패로 — 다음 loop 의
    //  serialEvent 가 그 명령을 정상 처리하고, 재접촉이 성공한다(명령 유실 없음).
    Serial.println(F("PSOk"));
    for (uint8_t i = 0; i < 55; ++i)
    {
        delay(10);
        if (Serial.available() <= 0) continue;
        if (Serial.peek() != 'Z') return false;
        (void)Serial.read();   // 'Z' 만 소비
        return true;
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

void SerialProcessor::legacy_print_block(const uint8_t sectorTrailer, const uint8_t block, const bool zeroOnly)
{
    // 읽기 실패 시 0으로 덤프 (1.0과 동일 — 라인 자체는 항상 출력).
    // ★와이어 포맷은 계약이라 바꾸지 않되, 실패 사실은 기억해 두었다가
    //  전송 후 LCD 로 알린다. 그러지 않으면 "정상적으로 보이는 0 데이터"가
    //  서버에 저장되고 아무 흔적도 남지 않는다 (2.2.5).
    memset(mLegacyBuffer, 0, MIFARE_BLOCK_SIZE);
    // zeroOnly: 폐기 블록의 소거가 실패한 경우 — 읽지 않고 0 줄을 낸다(옛 값을 보내지 않는다).
    if (!zeroOnly && mScanner.Read(block, mLegacyBuffer, MIFARE_BLOCK_SIZE) != RfidResult::Ok)
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
    const int number = str_atoi(string);
    if (number < 0) return -1;   // 빈값·비숫자·범위 밖은 발급 실패 — 종전엔 -1(0xFFFF) 번호 태그가 'new tag' 됐다(5차 D)
    mCachedTag.Number = number;

    // serial: 두 번째 ';'까지.
    memset(string, 0, sizeof(string));
    const auto idx2 = static_cast<int>(str_index_of_range(buffer, ';', idx + 1));
    if (idx2 == -1) return -1;
    str_substring_safe(buffer, string, 16, idx + 1, idx2);
    memcpy(mCachedTagSerial.Serial, string, sizeof(mCachedTagSerial.Serial));

    return SCOPE_TYPE_TAG;
}
