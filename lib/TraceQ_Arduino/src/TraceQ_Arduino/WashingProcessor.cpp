#include "WashingProcessor.hpp"

void WashingProcessor::WashingProcess(int deviceNumber, const AlarmOption &alarmOption,
                                      const ManagerOption &managerOption, const RecordOption &recordOption,
                                      DefaultRtc &rtc, LcdPrinter &printer)
{
    if (!is_valid(recordOption, managerOption, printer)) return;
    const bool isEnd = (mCachedProcess.Rewrite == 1);

    if (!try_load_manager_data(managerOption, isEnd, recordOption.GetManagerDisposability(), printer))
        return;

    if (isEnd)
    {
        WashingRecord record{deviceNumber, rtc.GetCurrentLocalDateTime()};
        washing_end(record);
        rtc.ClearAlarm(1);
    }
    else
    {
        washing_start(deviceNumber, alarmOption, rtc);
        rtc.SetAlarm(1, alarmOption.GetTimeSlot1(), 0);
    }
    complete_delay();

    if (hasnt_patient_info(recordOption))
        printer.CustomWarning(0, 2, 100, 4, F("No Patient Info"));
    else
        util_buzzer();
}

void WashingProcessor::update_process(int deviceNumber)
{
    auto current = mCachedProcess.Status;
    if (current != 0 && current != 1)
    {
        uint8_t tempStatus = 0;
        if (current == 2)
        {
            // 1.0과 동일하나, 2.0의 섹터 캐시 덕에 같은 섹터 내 Clear는 인증 1회로 완료.
            mScanner.Clear(SECTOR1_GATEWAY);
            mScanner.Clear(SECTOR2_PATIENT_KEY);
            mScanner.Clear(SECTOR2_PATIENT_NAME);
            mScanner.Clear(SECTOR15_EXAMINATION_SUBJECT);
        }
        if (current == 3) tempStatus = 1;
        current = tempStatus;
    }
    mCachedProcess = Process{current};
    mCachedProcess.MachineNumber = deviceNumber;
    mCachedProcess.WashingStatus = 1;
    mCachedProcess.LegacyRewrite = 0;
    mCachedProcess.Rewrite       = 1;
}

void WashingProcessor::washing_start(int deviceNumber, const AlarmOption &alarmOption, DefaultRtc &rtc)
{
    const auto current = rtc.GetCurrentDateTime();
    WashingRecord record{
        deviceNumber,
        LocalDateTime{
            LocalDate{current.year(), current.month(), current.day()},
            LocalTime{current.hour(), current.minute(), current.second()}}};

    update_process(deviceNumber);

    if (mScanner.Write(SECTOR2_WASHING_START, &record, 10) != RfidResult::Ok) return;
    if (!write_manager_key(SECTOR3_WASHING_START_MANAGER_KEY)) return;
    if (!write_manager_name(SECTOR3_WASHING_START_MANAGER_NAME)) return;
    if (!write_process()) return;

    record.DateTime = add_datetime(current, alarmOption.GetTimeSlot1(), record.DateTime.Time.Second);
    washing_end(record);
}

void WashingProcessor::washing_end(WashingRecord &record)
{
    if (mScanner.Write(SECTOR3_WASHING_END, &record, 10) != RfidResult::Ok) return;
    if (!write_manager_key(SECTOR4_WASHING_END_MANAGER_KEY)) return;
    if (!write_manager_name(SECTOR4_WASHING_END_MANAGER_NAME)) return;
}
