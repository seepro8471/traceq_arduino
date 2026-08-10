#include "DisinfectionProcessor.hpp"

void DisinfectionProcessor::DisinfectionProcess(
    int deviceNumber, const AlarmOption &alarmOption,
    DisinfectionOption &disinfectionOption, const ManagerOption &managerOption,
    const RecordOption &recordOption, DefaultRtc &rtc, LcdPrinter &printer)
{
    if (!is_valid(recordOption, managerOption, printer)) return;
    if (!mCachedProcess.WashingStatus)
    {
        printer.CustomWarning(0, 2, 100, 4, F("No Washing Info"));
        return;
    }
    const bool isMoved = mCachedProcess.MovementNeeded;
    bool isEnd  = (mCachedProcess.Rewrite == 2);

    if (!try_load_manager_data(managerOption, isEnd, recordOption.GetManagerDisposability(), printer))
        return;

    bool isGuest = false;
    bool simultaneously = disinfectionOption.GetSimultaneousDisinfectionSlot() >= 2;
    if (simultaneously)
    {
        if (!is_host(mCachedTag.Number)) isGuest = is_guest(mCachedTag.Number);
    }

    if (isEnd)
    {
        if (DefaultRtc::AddTimeSpan(mStartTime, 0, 2) > rtc.GetCurrentDateTime())
        {
            simultaneously = false;
            isEnd = false;
        }
    }

    if (mMovable && !isEnd) mMovable = false;

    if (isEnd)
    {
        if (mMovable)
        {
            disinfector_move(deviceNumber, isMoved, rtc);
        }
        else
        {
            DisinfectionRecord endRecord{deviceNumber, rtc.GetCurrentLocalDateTime()};
            disinfection_end(isMoved, endRecord);
        }
        if (isGuest) mGuestScopeNumber = static_cast<uint8_t>(-1);
        else { mHostScopeNumber = static_cast<uint8_t>(-1); mStartTime = DateTime{}; }
        rtc.ClearAlarm(2);
    }
    else
    {
        if (simultaneously)
        {
            const auto deadline = DefaultRtc::AddTimeSpan(
                mStartTime,
                static_cast<int8_t>(disinfectionOption.GetSimultaneousDisinfectionDelay()), 0);
            if (deadline >= rtc.GetCurrentDateTime()) isGuest = true;
        }
        disinfection_start(deviceNumber, isMoved, isGuest, alarmOption, disinfectionOption, rtc);
        if (isGuest) set_guest(mCachedTag.Number);
        else         set_host(mCachedTag.Number, rtc.GetCurrentDateTime());
        rtc.SetAlarm(2, alarmOption.GetTimeSlot2(), 0);
    }
    complete_delay();

    if (disinfectionOption.GetMaximumCount() != 0)
    {
        if (disinfectionOption.GetMaximumCount() <= disinfectionOption.GetCount())
        {
            printer.Notify(0, 2, 500, F("MaxCount Over"));
            return;
        }
    }
    if (hasnt_patient_info(recordOption))
        printer.Warning(0, 2, F("No Patient Info"));
    else
        util_buzzer();
}

void DisinfectionProcessor::disinfector_move(int deviceNumber, bool isMoved, DefaultRtc &rtc)
{
    mCachedProcess.MovementNeeded = true;
    mCachedProcess.Rewrite        = 0;
    if (!write_process()) return;
    DisinfectionRecord record{deviceNumber, rtc.GetCurrentLocalDateTime()};
    disinfection_end(isMoved, record);
}

void DisinfectionProcessor::disinfection_start(
    int deviceNumber, bool isMoved, bool isGuest, const AlarmOption &alarmOption,
    DisinfectionOption &disinfectionOption, DefaultRtc &rtc)
{
    if (!isMoved)
    {
        mScanner.ClearSector(5);
        mScanner.ClearSector(6);
    }

    DisinfectionDetail detail{};
    const uint8_t detailBlock = isMoved ? SECTOR14_DISINFECTION_DETAIL2 : SECTOR14_DISINFECTION_DETAIL;
    if (mScanner.Read(detailBlock, &detail, 10) != RfidResult::Ok) return;

    detail.GroupNumber = isGuest ? 2 : 1;
    detail.DateTime = disinfectionOption.IsClearDateTimeEmpty()
        ? rtc.GetCurrentLocalDateTime()
        : disinfectionOption.GetClearDateTime();

    Process newProcess{mCachedProcess.Status, 0, mCachedProcess.WashingStatus, 0,
                       deviceNumber, mCachedProcess.MovementNeeded, 0, 2};
    if (isMoved)
    {
        newProcess.DisinfectionStatus = 2;
        newProcess.DisinfectionCount  = 2;
    }
    else
    {
        newProcess.DisinfectionStatus = 1;
        newProcess.DisinfectionCount  = 1;
    }
    mCachedProcess = newProcess;

    const auto current = get_adjuest_start_time(rtc.GetCurrentDateTime(), rtc);
    if (current == DateTime{static_cast<uint32_t>(0)}) return;

    DisinfectionRecord record{
        deviceNumber,
        LocalDateTime{
            LocalDate{current.year(), current.month(), current.day()},
            LocalTime{current.hour(), current.minute(), current.second()}}};

    if (!write_process()) return;

    const uint8_t startBlk = isMoved ? SECTOR7_DISINFECTION_START : SECTOR5_DISINFECTION_START;
    const uint8_t keyBlk   = isMoved ? SECTOR7_DISINFECTION_START_MANAGER_KEY  : SECTOR5_DISINFECTION_START_MANAGER_KEY;
    const uint8_t nameBlk  = isMoved ? SECTOR7_DISINFECTION_START_MANAGER_NAME : SECTOR5_DISINFECTION_START_MANAGER_NAME;

    if (mScanner.Write(startBlk, &record, 10) != RfidResult::Ok) return;
    if (mScanner.Write(keyBlk,   mCachedTag.ID, 14) != RfidResult::Ok) return;
    if (mScanner.Write(nameBlk,  mCachedTagSerial.Serial, 16) != RfidResult::Ok) return;
    if (mScanner.Write(detailBlock, &detail, 10) != RfidResult::Ok) return;

    if (!isGuest) disinfectionOption.IncrementCount();

    record.DateTime = add_datetime(current, alarmOption.GetTimeSlot2(), record.DateTime.Time.Second);
    disinfection_end(isMoved, record);
}

void DisinfectionProcessor::disinfection_end(bool isMoved, DisinfectionRecord &record)
{
    const uint8_t recBlk  = isMoved ? SECTOR8_DISINFECTION_END : SECTOR6_DISINFECTION_END;
    const uint8_t keyBlk  = isMoved ? SECTOR8_DISINFECTION_END_MANAGER_KEY  : SECTOR6_DISINFECTION_END_MANAGER_KEY;
    const uint8_t nameBlk = isMoved ? SECTOR8_DISINFECTION_END_MANAGER_NAME : SECTOR6_DISINFECTION_END_MANAGER_NAME;

    if (mScanner.Write(recBlk,  &record, 10) != RfidResult::Ok) return;
    if (mScanner.Write(keyBlk,  mCachedTag.ID, 14) != RfidResult::Ok) return;
    if (mScanner.Write(nameBlk, mCachedTagSerial.Serial, 16) != RfidResult::Ok) return;
}

DateTime DisinfectionProcessor::get_adjuest_start_time(DateTime current, DefaultRtc &rtc)
{
    WashingRecord record{};
    if (mScanner.Read(SECTOR2_WASHING_START, &record, 10) != RfidResult::Ok)
        return DateTime{static_cast<uint32_t>(0)};

    const auto startTime = DefaultRtc::ToDateTime(record.DateTime);
    // 세척 시작 시각이 현재 시각보다 늦으면 소독기 RTC가 초기화된 것으로 판단,
    // "세척 종료 시각 + 1분"으로 소독기 RTC를 복구하고 그 시각을 소독 시작
    // 시각으로 쓴다. (+1분 = 세척기→소독기 이동 시간 반영, 2026-08-09 사용자 확정.
    // 1.0은 세척 종료 시각 그대로였음 — 복구 동작 자체는 1.0과 동일)
    if (startTime >= current)
    {
        WashingRecord endRecord{};
        if (mScanner.Read(SECTOR3_WASHING_END, &endRecord, 10) != RfidResult::Ok)
            return DateTime{static_cast<uint32_t>(0)};
        const auto endTime = DefaultRtc::AddTimeSpan(DefaultRtc::ToDateTime(endRecord.DateTime), 1, 0);
        rtc.SetDateTime(endTime);
        return endTime;
    }
    return current;
}
