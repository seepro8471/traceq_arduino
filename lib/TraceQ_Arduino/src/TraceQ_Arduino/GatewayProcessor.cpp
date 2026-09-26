#include "GatewayProcessor.hpp"

#include "TraceQ_Arduino/avr/AvrString.hpp"

void GatewayProcessor::GatewaySerialEvent(const char *buffer, DefaultRtc &rtc)
{
    if (buffer == nullptr) return;

    // ★환자정보 한 벌(키·이름·검사항목·검사일시)은 **같은 패킷에서 온 것만** 쓴다.
    //  2.2.5 는 검사일시만 비웠는데, 그래서 G3 구간이 없는 패킷 하나가 오면 형제(키·이름·검사항목)는
    //  직전 검사 것이 남아 "남의 환자 + 검사일시 0" 이 성공음과 함께 다음 스코프에 기록됐다.
    mHasPatientInformation = false;
    mDateTime = LocalDateTime{};
    memset(mPatientKey, 0, sizeof(mPatientKey));
    memset(mPatientName, 0, sizeof(mPatientName));
    memset(mExaminationSubject, 0, sizeof(mExaminationSubject));
    memset(mExaminationSubject2, 0, sizeof(mExaminationSubject2));
    memset(mExaminationSubject3, 0, sizeof(mExaminationSubject3));

    char string[64]{};

    // ★여러 레코드가 한 버퍼에 오면(수신 대기 1초 · 부팅 선택 화면 10초) **마지막 레코드**만 본다.
    //  마지막 `G1`(가장 최근 레코드 시작)부터로 좁힌다. 그 레코드가 도중에 잘렸으면 뒤 마커(G3..G4 등)가
    //  없어 아래에서 폴백된다(스테일·혼합 환자를 기록하지 않는다). 모든 필드가 같은 레코드에서 오므로
    //  서로 다른 환자의 조각이 섞이지 않는다.
    const char *rec = buffer;
    for (size_t at = str_index_of_cstr(buffer, "G1"); at != static_cast<size_t>(-1);
         at = str_index_of_cstr_range(buffer, "G1", at + 1))
        rec = buffer + at;

    // G1 = 본체번호 (2.2.6, 사용자 확정). `G1{gate};G2…` 형식.
    // 0(=`0000`)이면 "지정 없음"으로 보고 기기 자체 번호를 쓴다(effective_number).
    // 1.0 은 이 구간을 아예 읽지 않고 항상 기기 자체 번호를 썼다.
    if (find_string(rec, string, sizeof(string), "G1", "G2"))
    {
        const size_t sep = str_index_of(string, ';');
        if (sep != static_cast<size_t>(-1))
        {
            char gateText[8]{};
            str_substring_safe(string, gateText, sizeof(gateText), 0, sep);
            const int parsed = str_atoi(gateText);   // "0002" → 2, 비숫자면 -1
            if (parsed >= 0) mGateNumber = static_cast<int16_t>(parsed);
        }
    }
    memset(string, 0, sizeof(string));
    if (!find_string(rec, string, sizeof(string), "G3", "G4")) return;

    mHasPatientInformation = substring_for_patient(string);
    if (!mHasPatientInformation) return;

    memset(string, 0, sizeof(string));
    if (find_string(rec, string, sizeof(string), "G4", "G5"))
        substring_for_examination_subject(string);

    memset(string, 0, sizeof(string));
    if (find_string(rec, string, sizeof(string), "G2", "G3"))
        substring_for_local_date_time(string);

    const auto respDateTime = DefaultRtc::ToDateTime(mDateTime);
    if (respDateTime > rtc.GetCurrentDateTime())
        rtc.SetDateTime(DefaultRtc::AddTimeSpan(respDateTime, 0, 2));
}

void GatewayProcessor::GatewayProcess(int deviceNumber, LcdPrinter &printer)
{
    if (!is_valid(printer)) return;

    // 기록 실패를 조용히 넘기면 환자정보가 들어간 줄 안다 — 알리고 다시 대게(Sm! 도 안 보낸다).
    if (!write_patient_info(deviceNumber))
    {
        printer.CustomWarning(0, 2, 100, 4, F("Write Error"));
        return;
    }

    Serial.println(F("S;"));
    print_to_allnun(3, SECTOR0_TAG, reinterpret_cast<unsigned char *>(&mCachedTag));
    print_to_allnun(7, SECTOR1_TAG_SERIAL, mCachedTagSerial.Serial);
    Serial.println(F("Sm!"));

    complete_delay();
    util_buzzer();

    char buffer[11]{};
    snprintf(buffer, sizeof(buffer), "Scope : %02d", mCachedTag.Number);
    printer.InfoForWhile_cstr(0, 2, 500, buffer);
}

bool GatewayProcessor::write_no_patient_info(const Gateway &gateway, const char *stringDateTime)
{
    if (mScanner.Write(SECTOR1_GATEWAY, &gateway, 10) != RfidResult::Ok) return false;
    if (!write_process()) return false;

    // SECTOR2 환자 키/이름 두 블록 일괄 0으로 — 인증 1회.
    unsigned char zero[2 * MIFARE_BLOCK_SIZE]{};
    if (mScanner.WriteBlocks(SECTOR2_PATIENT_KEY, 2, zero) != RfidResult::Ok) return false;

    if (mScanner.ClearSector(15) != RfidResult::Ok) return false;
    return mScanner.Write(SECTOR15_EXAMINATION_SUBJECT, stringDateTime, 16) == RfidResult::Ok;
}

bool GatewayProcessor::write_patient_info(int deviceNumber)
{
    // 본체번호는 PC 가 G1 로 준 값 (기기 설정은 미수신 시 폴백일 뿐).
    Gateway gateway{effective_number(deviceNumber), mDateTime};
    if (mScanner.Write(SECTOR1_GATEWAY, &gateway, 10) != RfidResult::Ok) return false;
    if (mScanner.Write(SECTOR2_PATIENT_KEY,  mPatientKey,  16) != RfidResult::Ok) return false;
    if (mScanner.Write(SECTOR2_PATIENT_NAME, mPatientName, 16) != RfidResult::Ok) return false;

    // 1.0과 동일하게 섹터15를 먼저 소거 — 아래 일괄 쓰기가 중간에 실패해도
    // 이전 검사항목이 태그에 잔존하지 않도록 한다.
    if (mScanner.ClearSector(15) != RfidResult::Ok) return false;

    // SECTOR15 데이터 블록은 60,61,62 (트레일러 63). 3블록을 한 번에 일괄 쓰기 — 인증 1회.
    unsigned char buf[3 * MIFARE_BLOCK_SIZE]{};
    memcpy(buf,                          mExaminationSubject,  16);
    memcpy(buf + MIFARE_BLOCK_SIZE,      mExaminationSubject2, 16);
    memcpy(buf + 2 * MIFARE_BLOCK_SIZE,  mExaminationSubject3, 16);
    if (mScanner.WriteBlocks(SECTOR15_EXAMINATION_SUBJECT, 3, buf) != RfidResult::Ok) return false;

    // Status=1(환자정보 기록됨)은 **커밋 플래그** — 환자·검사항목 기록이 모두
    // 성공한 뒤에 마지막으로 세운다. 앞에 두면 중간 실패 시 "환자정보 있음"
    // 인데 실제 블록은 비어/이전 환자인 태그가 남아 세척기의 미기재 경고까지
    // 무력화된다 (1.0 승계 결함 — 2.2.5 수정). 최종 바이트는 동일.
    mCachedProcess.Status = 1;
    return write_process();
}

void GatewayProcessor::GatewayProcessFallback(int deviceNumber, DefaultRtc &rtc, LcdPrinter &printer)
{
    if (!is_valid(printer)) return;
    Serial.println(F("Not Patient Info"));
    mCachedProcess.Status = 0;

    const auto dateTime = rtc.GetCurrentDateTime();
    Gateway gateway{effective_number(deviceNumber), DefaultRtc::ToLocalDateTime(dateTime)};
    char format[16]{"YYYYMMDD:hhmmss"};
    const auto stringDateTime = dateTime.toString(format);

    // 기록 실패는 성공으로 알리지 않는다 — 알리고 다시 대게.
    if (!write_no_patient_info(gateway, stringDateTime))
    {
        printer.CustomWarning(0, 2, 100, 4, F("Write Error"));
        return;
    }

    complete_delay();
    // 환자정보 없이 기록했다 — 기록은 됐으므로 실패음(짧게 4회)과 달라야 한다. 길게 2회로 구분 (사장님 09-23).
    util_buzzer(400, 2);
}

bool GatewayProcessor::is_valid(LcdPrinter &printer)
{
    // 읽기 실패를 조용히 넘기면 환자정보가 들어간 줄 안다 — 세척·소독기와 같은 알림(RecordProcessor::is_valid).
    if (!print_tag_number(printer) || !read_tag_serial() || !read_process())
    {
        printer.CustomWarning(0, 2, 100, 4, F("Read Error"));
        return false;
    }
    if (mCachedProcess.WashingStatus != 0 && mCachedProcess.DisinfectionStatus != 0)
    {
        // CustomDebug 는 시리얼에도 "No Complete" 를 에코한다 — SeePro 가 이
        // 문자열로 완료 미처리 음성·화면 알림을 낸다 (올눈 MainFormSo 58395
        // 재현). 문구를 바꾸면 안 된다.
        printer.RejectDebug(0, 2, F("No Complete"));
        return false;
    }
    return true;
}

bool GatewayProcessor::find_string(const char *src, char *dst, size_t dstSize, const char *from, const char *to)
{
    if (src == nullptr || dst == nullptr || from == nullptr || to == nullptr) return false;
    // ★한 레코드 안에서는 **첫 일치**를 쓴다 — 값에 `G3` 같은 마커가 들어 있어도(예: 등록키 "G3PAT")
    //  뒤에서 잘못 집지 않게. 여러 레코드가 한 버퍼에 온 경우는 호출 전에 마지막 레코드로 좁혀 둔다.
    const auto fromIdx = str_index_of_cstr(src, from);
    if (fromIdx == static_cast<size_t>(-1)) return false;
    const auto toIdx = str_index_of_cstr_range(src, to, fromIdx + str_strlen(from));
    if (toIdx == static_cast<size_t>(-1)) return false;
    str_substring_safe(src, dst, dstSize, fromIdx + str_strlen(from), toIdx);
    return true;
}

bool GatewayProcessor::substring_for_patient(const char *string)
{
    // [17] = 블록 16바이트를 다 담기 위한 크기(str_substring_safe 는 NUL 자리를 남긴다).
    // 15바이트만 담던 때는 올눈이 보내는 16바이트 조각의 끝 한글이 반쪽으로 남았다 (09-23).
    char data[17]{};
    const auto keyIdx = str_index_of(string, ';');
    if (keyIdx == static_cast<size_t>(-1)) return false;

    str_substring_safe(string, data, sizeof(data), 0, keyIdx);
    if (str_strlen(data) == 0) return false;
    memcpy(mPatientKey, data, sizeof(mPatientKey));

    const auto nameIdx = str_index_of_range(string, ';', keyIdx + 1);
    if (nameIdx != static_cast<size_t>(-1))
    {
        memset(data, 0, sizeof(data));
        str_substring_safe(string, data, sizeof(data), keyIdx + 1, nameIdx);
        memcpy(mPatientName, data, sizeof(mPatientName));
    }
    return true;
}

void GatewayProcessor::substring_for_examination_subject(const char *string)
{
    memset(mExaminationSubject,  0, 16);
    memset(mExaminationSubject2, 0, 16);
    memset(mExaminationSubject3, 0, 16);

    char data[17]{};   // 블록 16바이트 전부 (위 substring_for_patient 주석 참조)
    const auto firstIdx = str_index_of_cstr(string, ";");
    if (firstIdx == static_cast<size_t>(-1)) return;
    str_substring_safe(string, data, sizeof(data), 0, firstIdx);
    if (str_strlen(data) != 0) memcpy(mExaminationSubject, data, 16);

    memset(data, 0, sizeof(data));
    const auto secondIdx = str_index_of_range(string, ';', firstIdx + 1);
    if (secondIdx != static_cast<size_t>(-1))
    {
        str_substring_safe(string, data, sizeof(data), firstIdx + 1, secondIdx);
        if (str_strlen(data) != 0) memcpy(mExaminationSubject2, data, 16);
    }

    memset(data, 0, sizeof(data));
    const auto thirdIdx = (secondIdx != static_cast<size_t>(-1))
        ? str_index_of_range(string, ';', secondIdx + 1)
        : static_cast<size_t>(-1);
    if (thirdIdx != static_cast<size_t>(-1))
    {
        str_substring_safe(string, data, sizeof(data), secondIdx + 1, thirdIdx);
    }
    else if (str_strlen(string) > 48 && secondIdx != static_cast<size_t>(-1))
    {
        str_substring_safe(string, data, sizeof(data), secondIdx + 1, 48);
    }
    if (str_strlen(data) != 0) memcpy(mExaminationSubject3, data, 16);
}

void GatewayProcessor::substring_for_local_date_time(char *string)
{
    mDateTime = LocalDateTime{};

    const int year   = str_atoi(strtok(string, ";"));
    if (year   == -1) return;
    const int month  = str_atoi(strtok(nullptr, ";"));
    if (month  == -1) return;
    const int day    = str_atoi(strtok(nullptr, ";"));
    if (day    == -1) return;
    if (strtok(nullptr, ";") == nullptr) return;
    const int hour   = str_atoi(strtok(nullptr, ";"));
    if (hour   == -1) return;
    const int minute = str_atoi(strtok(nullptr, ";"));
    if (minute == -1) return;
    const int second = str_atoi(strtok(nullptr, ";"));
    if (second == -1) return;

    mDateTime = LocalDateTime{
        LocalDate{static_cast<uint16_t>(year), static_cast<uint8_t>(month), static_cast<uint8_t>(day)},
        LocalTime{static_cast<uint8_t>(hour), static_cast<uint8_t>(minute), static_cast<uint8_t>(second)}};
}

void GatewayProcessor::print_to_allnun(uint8_t sectorTrailer, uint8_t block, unsigned char *buffer)
{
    if (sectorTrailer < 0x10) Serial.print('0');
    Serial.print(sectorTrailer, HEX);
    if (block < 0x10) Serial.print('0');
    Serial.print(block, HEX);
    for (uint8_t i = 0; i < 16; ++i)
    {
        if (buffer[i] < 0x10) Serial.print('0');
        Serial.print(buffer[i], HEX);
    }
    Serial.println(';');
}
