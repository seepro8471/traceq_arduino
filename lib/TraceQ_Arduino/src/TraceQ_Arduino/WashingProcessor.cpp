#include "WashingProcessor.hpp"

void WashingProcessor::WashingProcess(int deviceNumber, const AlarmOption &alarmOption,
                                      const ManagerOption &managerOption, const RecordOption &recordOption,
                                      DefaultRtc &rtc, LcdPrinter &printer)
{
    if (is_valid(recordOption, managerOption, printer) <= 0) return;
    bool isEnd = (mCachedProcess.Rewrite == 1);

    if (!try_load_manager_data(managerOption, isEnd, recordOption.GetManagerDisposability(), printer))
        return;

    // 더블터치 = 태그에 적힌 시작이 2초 안 — 종료가 아니라 시작 다시 하기(소독기와 같은 규칙).
    if (isEnd)
    {
        const int8_t just = started_just_now(SECTOR2_WASHING_START, rtc);
        if (just < 0)
        {
            printer.CustomWarning(0, 2, 100, 4, F("Read Error"));   // 판정 불가 — 알람·기록 그대로, 다시 대게
            return;
        }
        if (just) isEnd = false;
    }

    if (isEnd)
    {
        WashingRecord record{deviceNumber, rtc.GetCurrentLocalDateTime()};
        // 종료 기록 실패도 성공으로 알리지 않는다 — 알람을 남겨 두고 재접촉을 유도.
        if (!washing_end(record))
        {
            printer.CustomWarning(0, 2, 100, 4, F("Write Error"));
            return;
        }
        rtc.ClearAlarm(1);
    }
    else
    {
        // 커밋 전 실패는 성공으로 알리지 않는다 — 알람 없이 재접촉을 유도.
        if (!washing_start(deviceNumber, alarmOption, rtc))
        {
            printer.CustomWarning(0, 2, 100, 4, F("Write Error"));
            return;
        }
        consume_disposability();   // 일회성 담당자는 커밋된 시작에만 쓰인다
        rtc.SetAlarm(1, alarmOption.GetTimeSlot1(), 0);
    }
    complete_delay();

    if (hasnt_patient_info(recordOption))
        // 환자정보 없음 = 길게 2회(기록은 됐다). 실패(짧게 4회)와 구분 (사장님 09-23).
        printer.CustomWarning(0, 2, 400, 2, F("No Patient Info"));
    else
        util_buzzer();
}

// [5차 판정 · 재론 금지] Status==2 레거시 갈래의 Clear×4 반환 무시 — 2.0 은 Status 에 0·1 만 쓴다(델파이 2/3 기록 자리 없음).
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

bool WashingProcessor::washing_start(int deviceNumber, const AlarmOption &alarmOption, DefaultRtc &rtc)
{
    const auto current = rtc.GetCurrentDateTime();
    WashingRecord record{
        deviceNumber,
        LocalDateTime{
            LocalDate{current.year(), current.month(), current.day()},
            LocalTime{current.hour(), current.minute(), current.second()}}};

    update_process(deviceNumber);

    if (mScanner.Write(SECTOR2_WASHING_START, &record, 10) != RfidResult::Ok) return false;
    if (!write_manager_key(SECTOR3_WASHING_START_MANAGER_KEY)) return false;
    if (!write_manager_name(SECTOR3_WASHING_START_MANAGER_NAME)) return false;

    // ★미리 채우는 자동 종료도 **커밋 앞**에 둔다 — "종료 시각이 빌 수 없다"(사장님 09-23)가 안전망인데,
    //  커밋 뒤에 두면 실패해도 성공음이 나서 "시작은 있고 종료는 0" 인 태그가 조용히 나갔다.
    WashingRecord autoEnd{record};
    autoEnd.DateTime = add_datetime(current, alarmOption.GetTimeSlot1(), record.DateTime.Time.Second);
    if (!washing_end(autoEnd)) return false;

    return write_process();   // 커밋은 마지막
}

bool WashingProcessor::washing_end(WashingRecord &record)
{
    // ★담당자를 먼저, **시각을 마지막에** — 반대면 담당자 블록만 실패했을 때 "오늘 종료 시각 + 지난
    //  주기 담당자" 쌍이 남는다(그 블록들은 서버 덤프도 안 지워 옛 담당자가 늘 남아 있다).
    if (!write_manager_key(SECTOR4_WASHING_END_MANAGER_KEY)) return false;
    if (!write_manager_name(SECTOR4_WASHING_END_MANAGER_NAME)) return false;
    return mScanner.Write(SECTOR3_WASHING_END, &record, 10) == RfidResult::Ok;
}
