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
    DateTime startDt{};
    bool isRestart = false;   // 더블터치 가드로 시작이 재실행됐는지(소독기와 같은 이름)
    if (isEnd)
    {
        const int8_t just = started_just_now(SECTOR2_WASHING_START, rtc, &startDt);
        if (just < 0)
        {
            printer.CustomWarning(0, 2, 100, 4, F("Read Error"));   // 판정 불가 — 알람·기록 그대로, 다시 대게
            return;
        }
        if (just) { isEnd = false; isRestart = true; }
    }

    if (isEnd)
    {
        WashingRecord record{deviceNumber, not_before_start(startDt, rtc.GetCurrentLocalDateTime())};
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
        if (!washing_start(deviceNumber, alarmOption, rtc, isRestart))
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

bool WashingProcessor::washing_start(int deviceNumber, const AlarmOption &alarmOption, DefaultRtc &rtc,
                                     bool isRestart)
{
    const auto current = rtc.GetCurrentDateTime();
    WashingRecord record{
        deviceNumber,
        LocalDateTime{
            LocalDate{current.year(), current.month(), current.day()},
            LocalTime{current.hour(), current.minute(), current.second()}}};

    // ★지난 주기 표지를 **먼저 내린다** — 커밋 전에 접촉이 끊기면 태그가 '공정 없음' 이라 소독기·서버가
    //  거부음으로 알린다. 안 내리면 지난 주기 Rewrite=2 가 남아 소독기가 그 접촉을 소독 '종료' 로 기록했다.
    //  ★재시작(더블터치)은 건너뛴다 — 이번 주기 커밋이 이미 지웠고, 그것을 다시 0 으로 내렸다가 찢기면
    //   **커밋된 이번 주기 세척 시작이 사라진다**(소독기·서버가 그 주기를 거부한다). 소독 시작의 `isRestart`
    //   소거 건너뛰기와 같은 규칙.
    //  [10차 판정] 대가: 아직 서버에 안 올린 **지난 주기**는 이 선행 쓰기 시점(접촉 초반)에 덤프 불가가 된다
    //   — 종전엔 커밋(접촉 끝)까지 살았다. 성공한 세척 시작도 그 주기를 어차피 덮으므로 잃는 것은
    //   "찢긴 시도에서 몇 초 일찍" 뿐이고, 안 내리면 **안 한 소독이 완료로 대장에 남는다**(9회차 P1).
    const uint8_t prevStatus = mCachedProcess.Status;
    if (!isRestart)
    {
        mCachedProcess = Process{prevStatus};
        if (!write_process()) return false;

        // 환자정보 없는 태그면 옛 환자 블록도 비운다 — PC 는 Status 를 안 보고 블록 8·9 를 등록번호·이름으로 저장한다.
        //  ★0 과 **알 수 없는 값(3 초과)** 일 때 — 레거시 2/3 만 update_process 가 처리한다(3 은 '환자정보 있음'
        //   이라 비우면 "Status=1 인데 블록은 빈" 태그가 되고, 2 는 같은 블록을 두 번 지워 접촉 예산을 넘겼다).
        //   Status 는 태그에서 읽는 uint8_t 라 4~255 도 올 수 있고, 그 값은 update_process 의 Clear×4(current==2
        //   한정)도 안 타므로 == 0 만 보면 "Status 0 으로 커밋되는데 블록 8·9 엔 지난 환자" 가 남는다(11차 FF1).
        if (prevStatus == 0 || prevStatus > 3)
        {
            unsigned char zero[2 * MIFARE_BLOCK_SIZE]{};
            if (mScanner.WriteBlocks(SECTOR2_PATIENT_KEY, 2, zero) != RfidResult::Ok) return false;
        }
    }

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
