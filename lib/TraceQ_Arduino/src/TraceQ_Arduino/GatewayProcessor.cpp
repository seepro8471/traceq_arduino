#include "GatewayProcessor.hpp"

#include "TraceQ_Arduino/avr/AvrString.hpp"

void GatewayProcessor::GatewaySerialEvent(const char *buffer, DefaultRtc &rtc)
{
    if (buffer == nullptr) return;

    // G2(검사일시) 구간이 없는 패킷이 오면 이전 검사의 날짜가 그대로 남아
    // 태그에 기록됐다 — 매 수신마다 초기화한다 (1.0 승계 결함, 2.2.5).
    mDateTime = LocalDateTime{};

    char string[64]{};
    if (!find_string(buffer, string, sizeof(string), "G3", "G4")) return;

    mHasPatientInformation = substring_for_patient(string);
    if (!mHasPatientInformation) return;

    memset(string, 0, sizeof(string));
    if (find_string(buffer, string, sizeof(string), "G4", "G5"))
        substring_for_examination_subject(string);

    memset(string, 0, sizeof(string));
    if (find_string(buffer, string, sizeof(string), "G2", "G3"))
        substring_for_local_date_time(string);

    const auto respDateTime = DefaultRtc::ToDateTime(mDateTime);
    if (respDateTime > rtc.GetCurrentDateTime())
        rtc.SetDateTime(DefaultRtc::AddTimeSpan(respDateTime, 0, 2));
}

void GatewayProcessor::GatewayProcess(int deviceNumber, LcdPrinter &printer)
{
    if (!is_valid(printer)) return;

    Gateway gateway{deviceNumber, mDateTime};
    if (mScanner.Write(SECTOR1_GATEWAY, &gateway, 10) != RfidResult::Ok) return;
    if (mScanner.Write(SECTOR2_PATIENT_KEY,  mPatientKey,  16) != RfidResult::Ok) return;
    if (mScanner.Write(SECTOR2_PATIENT_NAME, mPatientName, 16) != RfidResult::Ok) return;

    // 1.0과 동일하게 섹터15를 먼저 소거 — 아래 일괄 쓰기가 중간에 실패해도
    // 이전 검사항목이 태그에 잔존하지 않도록 한다.
    if (mScanner.ClearSector(15) != RfidResult::Ok) return;

    // SECTOR15 데이터 블록은 60,61,62 (트레일러 63). 3블록을 한 번에 일괄 쓰기 — 인증 1회.
    unsigned char buf[3 * MIFARE_BLOCK_SIZE]{};
    memcpy(buf,                          mExaminationSubject,  16);
    memcpy(buf + MIFARE_BLOCK_SIZE,      mExaminationSubject2, 16);
    memcpy(buf + 2 * MIFARE_BLOCK_SIZE,  mExaminationSubject3, 16);
    if (mScanner.WriteBlocks(SECTOR15_EXAMINATION_SUBJECT, 3, buf) != RfidResult::Ok) return;

    // Status=1(환자정보 기록됨)은 **커밋 플래그** — 환자·검사항목 기록이 모두
    // 성공한 뒤에 마지막으로 세운다. 앞에 두면 중간 실패 시 "환자정보 있음"
    // 인데 실제 블록은 비어/이전 환자인 태그가 남아 세척기의 미기재 경고까지
    // 무력화된다 (1.0 승계 결함 — 2.2.5 수정). 최종 바이트는 동일.
    mCachedProcess.Status = 1;
    if (!write_process()) return;

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

void GatewayProcessor::GatewayProcessFallback(int deviceNumber, DefaultRtc &rtc, LcdPrinter &printer)
{
    if (!is_valid(printer)) return;
    Serial.println(F("Not Patient Info"));
    mCachedProcess.Status = 0;

    const auto dateTime = rtc.GetCurrentDateTime();
    Gateway gateway{deviceNumber, DefaultRtc::ToLocalDateTime(dateTime)};
    char format[16]{"YYYYMMDD:hhmmss"};
    const auto stringDateTime = dateTime.toString(format);

    if (mScanner.Write(SECTOR1_GATEWAY, &gateway, 10) != RfidResult::Ok) return;
    if (!write_process()) return;

    // SECTOR2 환자 키/이름 두 블록 일괄 0으로 — 인증 1회.
    unsigned char zero[2 * MIFARE_BLOCK_SIZE]{};
    if (mScanner.WriteBlocks(SECTOR2_PATIENT_KEY, 2, zero) != RfidResult::Ok) return;

    if (mScanner.ClearSector(15) != RfidResult::Ok) return;
    if (mScanner.Write(SECTOR15_EXAMINATION_SUBJECT, stringDateTime, 16) != RfidResult::Ok) return;

    complete_delay();
    util_buzzer(40, 4);
}

bool GatewayProcessor::is_valid(LcdPrinter &printer)
{
    if (!print_tag_number(printer) || !read_tag_serial()) return false;
    if (!read_process()) return false;
    if (mCachedProcess.WashingStatus != 0 && mCachedProcess.DisinfectionStatus != 0)
    {
        // CustomDebug 는 시리얼에도 "No Complete" 를 에코한다 — SeePro 가 이
        // 문자열로 완료 미처리 음성·화면 알림을 낸다 (올눈 MainFormSo 58395
        // 재현). 문구를 바꾸면 안 된다.
        printer.CustomDebug(0, 2, 100, 4, F("No Complete"));
        return false;
    }
    return true;
}

bool GatewayProcessor::find_string(const char *src, char *dst, size_t dstSize, const char *from, const char *to)
{
    if (src == nullptr || dst == nullptr || from == nullptr || to == nullptr) return false;
    const auto fromIdx = str_index_of_cstr(src, from);
    const auto toIdx   = str_index_of_cstr(src, to);
    if (fromIdx == static_cast<size_t>(-1) || toIdx == static_cast<size_t>(-1)) return false;
    str_substring_safe(src, dst, dstSize, fromIdx + str_strlen(from), toIdx);
    return true;
}

bool GatewayProcessor::substring_for_patient(const char *string)
{
    char data[16]{};
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

    char data[16]{};
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
