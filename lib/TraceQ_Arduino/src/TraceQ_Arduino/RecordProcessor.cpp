#include "RecordProcessor.hpp"

void RecordProcessor::SaveManagerData(const RecordOption &recordOption,
                                      ManagerOption &managerOption, LcdPrinter &printer)
{
    // 읽기 실패를 조용히 넘기면 담당자가 등록된 줄 안다 — 알리고 다시 대게.
    if (!print_tag_number(printer) || !read_tag_serial())
    {
        printer.CustomWarning(0, 2, 100, 4, F("Read Error"));
        return;
    }

    // Tag.ID는 14바이트인데 SetKey는 KEY_SIZE(16)바이트를 복사한다 —
    // 1.0은 인접 2바이트를 함께 읽는 OOB였음. 16바이트 버퍼로 0패딩 후 전달.
    unsigned char key[ManagerOption::KEY_SIZE]{};
    memcpy(key, mCachedTag.ID, sizeof(mCachedTag.ID));
    managerOption.SetKey(key);
    managerOption.SetName(mCachedTagSerial.Serial);

    if (recordOption.GetManagerDisposability()) mDisposabilityFlag = true;

    // Tag.ID[14]는 구조체 마지막 멤버라 14바이트가 다 차면 종료 NUL 이 없다 —
    // 그대로 출력하면 인접 전역 영역을 계속 읽는다 (2.2.5).
    char idText[sizeof(mCachedTag.ID) + 1]{};
    memcpy(idText, mCachedTag.ID, sizeof(mCachedTag.ID));
    printer.Notify_cstr(0, 2, 100, idText);
}

int8_t RecordProcessor::is_valid(const RecordOption &recordOption,
                                 const ManagerOption &managerOption, LcdPrinter &printer)
{
    if (!print_tag_number(printer) || !read_tag_serial())
    {
        printer.CustomWarning(0, 2, 100, 4, F("Read Error"));
        return -1;
    }
    if (!recordOption.GetManagerDisposability() && !managerOption.HasData())
    {
        printer.Reject(0, 2, F("No Manager Info"));
        return 0;
    }
    if (!read_process())
    {
        printer.CustomWarning(0, 2, 100, 4, F("Read Error"));
        return -1;
    }
    return 1;
}

bool RecordProcessor::write_manager_key(uint8_t addr)
{
    return mScanner.Write(addr, mCachedTag.ID, 14) == RfidResult::Ok;
}

bool RecordProcessor::write_manager_name(uint8_t addr)
{
    return mScanner.Write(addr, mCachedTagSerial.Serial, 16) == RfidResult::Ok;
}

bool RecordProcessor::try_load_manager_data(const ManagerOption &managerOption,
                                            bool isEnd, bool disposability, LcdPrinter &printer)
{
    if (disposability)
    {
        if (!isEnd && !mDisposabilityFlag)
        {
            printer.Reject(0, 2, F("No Manager Info"));
            return false;
        }
        // ★여기서 소모하지 않는다 — 시작 커밋이 성공한 뒤 consume_disposability() 로. 종전엔 실패한 시작·
        //  종료 터치에도 소모돼 Write Error 뒤 재접촉이 "No Manager Info" 가 됐다(5차 C).
    }
    load_manager_data(managerOption);
    return true;
}

bool RecordProcessor::hasnt_patient_info(const RecordOption &recordOption)
{
    return recordOption.GetPatientCheck() && mCachedProcess.Status != 1;
}

int8_t RecordProcessor::started_just_now(uint8_t startBlock, DefaultRtc &rtc)
{
    WashingRecord record{};   // 세척·소독 시작 기록은 레이아웃이 같다(번호 2 + 일시 8)
    if (mScanner.Read(startBlock, &record, 10) != RfidResult::Ok) return -1;   // 판정 불가 — '종료' 로 떨어뜨리지 않는다
    const auto started = DefaultRtc::ToDateTime(record.DateTime);
    if (!started.isValid()) return 0;
    const int32_t gap = (rtc.GetCurrentDateTime() - started).totalseconds();
    // [5차 판정 · 재론 금지] 2초 창은 그대로(사장님 09-27). 커밋 뒤 확인 실패로 'Write Error' 가 난 태그를
    //  2초 넘겨 다시 대면 종료가 되는 경우는, write_process 의 재확인(5차)으로 거의 사라지고 나머지는 감수.
    return gap > -2 && gap < 2;
}

LocalDateTime RecordProcessor::add_datetime(const DateTime &current,
                                            uint8_t minute, uint8_t seconds)
{
    const int32_t totalSeconds = minute * 60 + seconds;
    const auto added = current + TimeSpan{totalSeconds};
    return LocalDateTime{
        LocalDate{added.year(), added.month(), added.day()},
        LocalTime{added.hour(), added.minute(), added.second()}};
}

void RecordProcessor::load_manager_data(const ManagerOption &managerOption)
{
    managerOption.GetKey(mCachedTag.ID, 14);
    managerOption.GetName(mCachedTagSerial.Serial, 16);
}
