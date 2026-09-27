#include "DisinfectionProcessor.hpp"

void DisinfectionProcessor::DisinfectionProcess(
    int deviceNumber, const AlarmOption &alarmOption,
    DisinfectionOption &disinfectionOption, const ManagerOption &managerOption,
    const RecordOption &recordOption, DefaultRtc &rtc, LcdPrinter &printer)
{
    const int8_t valid = is_valid(recordOption, managerOption, printer);
    if (valid <= 0)
    {
        // 거부(담당자 없음, 0)로 끝나면 이동 플래그도 내린다(2.2.5). ★읽기 실패(-1)에는 지킨다 — 내리면 재접촉이
        //  이동이 아니라 종료로 기록돼 2차 소독이 사라졌다(5차 V2). 담당자 일회성과 같은 규칙: 실패는 소모하지 않는다.
        //  (is_valid 의 첫 읽기 실패를 조건식으로 흉내내던 것은 담당자 미등록 상태에서 틀렸다 — W1 P3-1 → 3진값)
        if (valid == 0) mMovable = false;
        return;
    }
    if (!mCachedProcess.WashingStatus)
    {
        printer.Reject(0, 2, F("No Washing Info"));
        mMovable = false;
        return;
    }
    const bool isMoved = mCachedProcess.MovementNeeded;
    bool isEnd  = (mCachedProcess.Rewrite == 2);
    bool isRestart = false;   // 더블터치 가드로 시작이 재실행됐는지

    if (!try_load_manager_data(managerOption, isEnd, recordOption.GetManagerDisposability(), printer))
    {
        mMovable = false;   // 위 두 거부와 같이 이동 플래그도 내린다
        return;
    }

    bool isGuest = false;
    bool simultaneously = disinfectionOption.GetSimultaneousDisinfectionSlot() >= 2;
    if (simultaneously)
    {
        if (!is_host(mCachedTag.Number)) isGuest = is_guest(mCachedTag.Number);
    }

    DateTime startDt{};
    if (isEnd)
    {
        // 더블터치 = 태그에 적힌 시작이 2초 안(host·guest 공통, 기록 실패·재부팅 뒤에도 태그가 답한다).
        const int8_t just = started_just_now(isMoved ? SECTOR7_DISINFECTION_START : SECTOR5_DISINFECTION_START,
                                             rtc, &startDt);
        if (just < 0)
        {
            printer.CustomWarning(0, 2, 100, 4, F("Read Error"));   // 판정 불가 — 알람·슬롯·이동 플래그 그대로, 다시 대게
            return;
        }
        if (just)
        {
            simultaneously = false;
            isEnd = false;
            isRestart = true;   // 시작 재실행 — 소독 횟수는 다시 올리지 않는다
        }
    }

    // 이동 플래그는 **새 시작에서만** 내린다(1.0 동일) — 액교환 뒤 욕조 안 스코프 여럿을 차례로 이동시키는
    //  것이 의도다. ★12차 정정: `!isEnd` 만 보면 **이미 이동한 스코프를 한 번 더 댄 것**(Rewrite=0 이라 isEnd
    //  가 거짓)도 '새 시작' 으로 세어 플래그를 내렸다 → 그 뒤 욕조에 남은 스코프 **전부가 조용히 1차 종료**로
    //  기록되고 2차 소독이 유실됐다(과소 계수 = 액교환이 늦어진다 · GG2 P2-1). 5차 판정은 피해를 "다시 댄 그
    //  스코프" 로만 봤는데 실제로는 **남은 전부**였다 — 첫 접촉이 성공음이라 사람이 알 수도 없다.
    //  이미 이동한 스코프는 새 시작이 아니므로 세지 않는다(이 한 낱말이 주석의 원래 뜻을 되살린다).
    // [12차 판정 · 재론 금지] 대가 하나: **도착 기기에서도 액교환을 한** 경우 이동 플래그가 2차 시작을 넘어
    //  살아남아, 그 스코프를 또 대면 2차 종료가 '3차 이동' 이 되고 한 번 더 대면 종료 시각이 미리채움으로
    //  덮이며 그 소독기 횟수가 1→2 가 된다(과다 계수 = 액교환이 일러지는 **안전한 방향** · GG1 §5 실측).
    //  액교환을 두 기기에서 하고 접촉을 세 번 더 해야 하는 좁은 조합이고, 막는 쪽(`!isMoved` 를 이동 선택에도
    //  넣기)은 `[5차 판정 ③]`("또 이동시키면 2차 기록이 덮인다 = 조작 오류 범위")과 부딪히므로 그대로 둔다.
    if (mMovable && !isEnd && !isMoved) mMovable = false;

    if (isEnd)
    {
        // ★종료는 **태그에 적힌 시작 기기에서만** — 이동 표시(mMovable)는 RAM 이라 그 소독기가 재부팅되거나
        //  다른 태그가 한 번 거부되면 사라지고, 그 뒤 도착 소독기에 대면 또 '종료' 로 처리돼 앞 기기의 종료
        //  시각·담당자를 **조용히 덮었다**(대장에서 도착기 소독이 사라져 소독 횟수 과소 = 액교환이 늦어진다 · FF2 P1-1).
        //  사장님 확인(09-27): 액교환 절차 없이 기기 간 이동하는 일도, 운용 중 기기번호를 바꾸는 일도 없다.
        //  Reject 는 시리얼 에코가 없어 PC 와이어 계약은 그대로다. [11차 판정 · 재론 금지] 이 거부를 PC 로그에도
        //  남기자(RejectDebug + 세척관리 문구 처리 = 양쪽 짝 출하)는 **하지 않는다**(사장님 09-27) — 사람은
        //  소독기 화면 글자와 거부음으로 안다.
        if (!mMovable && mCachedProcess.MachineNumber != deviceNumber)
        {
            printer.Reject(0, 2, F("Other Machine"));
            return;
        }
        bool ok;
        if (mMovable)
        {
            ok = disinfector_move(deviceNumber, isMoved, rtc, startDt);
        }
        else
        {
            // 형제 셋(세척 종료·소독 종료·이동 종료)에 같은 보정 — 7차엔 이 자리만 빠졌다(CC1 P1-2).
            //  사람 조작 없이도 방아쇠가 있다: 시작과 종료 사이에 RTC 전지가 방전되면 시계가 2026-01-01 로
            //  고정되고, 복구는 시작 경로에만 있어 종료 터치엔 안 듣는다 → 종료가 시작보다 268일 앞섰다.
            DisinfectionRecord endRecord{deviceNumber,
                                         not_before_start(startDt, rtc.GetCurrentLocalDateTime())};
            ok = disinfection_end(isMoved, endRecord);
        }
        // 종료·이동 기록 실패도 성공으로 알리지 않는다 — 슬롯·알람을 남겨 두고 재접촉을 유도.
        if (!ok)
        {
            printer.CustomWarning(0, 2, 100, 4, F("Write Error"));
            return;
        }
        // ★자기 슬롯만 비운다 — 슬롯이 없는 스코프(다른 스코프에 host 를 넘겨준 뒤 끝나는 것)의 종료가
        //  현재 host 의 슬롯·시간창을 지우면, 그 창 안에 온 다음 스코프가 guest 가 아닌 host 로 기록됐다(5차 C P1-1).
        if (isGuest) mGuestScopeNumber = kNoScope;
        else if (is_host(mCachedTag.Number)) { mHostScopeNumber = kNoScope; mStartTime = DateTime{}; }
        rtc.ClearAlarm(2);
    }
    else
    {
        if (simultaneously)
        {
            const auto deadline = DefaultRtc::AddTimeSpan(
                mStartTime,
                static_cast<int8_t>(disinfectionOption.GetSimultaneousDisinfectionDelay()), 0);
            // ★대입이어야 한다. 승격(|=)이면, 종료 터치 없이 회수된 guest 슬롯이
            //  남아 며칠 뒤 그 스코프를 단독 소독해도 계속 guest 로 판정되어
            //  소독 횟수가 오르지 않는다(액교환 주기 왜곡). 시간창 안에서만
            //  guest — 원래 의도대로 복원 (1.0 승계 결함, 2.2.5).
            // ★시간창은 host 시작 **뒤**만 — 시계를 뒤로 돌리면 지난 창이 되살아나, 단독 소독이 guest 로
            //  박히고 소독 횟수가 오르지 않아 그 소독이 액교환 주기에서 사라졌다(BB2 P2-3).
            const auto nowDt = rtc.GetCurrentDateTime();
            isGuest = (nowDt >= mStartTime && deadline >= nowDt);
        }
        // 커밋 전 실패는 성공으로 알리지 않는다 — host·알람 없이 재접촉을 유도.
        if (!disinfection_start(deviceNumber, isMoved, isGuest, isRestart,
                                alarmOption, disinfectionOption, rtc))
        {
            printer.CustomWarning(0, 2, 100, 4, F("Write Error"));
            return;
        }
        consume_disposability();   // 일회성 담당자는 커밋된 시작에만 쓰인다
        if (isGuest) set_guest(mCachedTag.Number);
        else         set_host(mCachedTag.Number, rtc.GetCurrentDateTime());
        rtc.SetAlarm(2, alarmOption.GetTimeSlot2(), 0);
    }
    complete_delay();

    if (disinfectionOption.GetMaximumCount() != 0)
    {
        if (disinfectionOption.GetMaximumCount() <= disinfectionOption.GetCount())
            // [13차 HH2 P3-5] 이 500×1 은 **클리어 태그 성공(액교환을 했다)** 과 같은 소리인데 뜻이 정반대다
            //  (액교환을 해야 한다). 접촉이 다르면 사람이 소리로 구별할 수 없다 — 바꾸는 것은 사장님 판정 사항.
            printer.Notify(0, 2, 500, F("MaxCount Over"));   // return 하지 않는다 — 아래 환자정보 경고를 가렸다
    }
    if (hasnt_patient_info(recordOption))
        // 환자정보 없음 = 길게 2회(세척기와 같은 소리 — 종전엔 40ms 4회로 달랐다).
        printer.CustomWarning(0, 2, 400, 2, F("No Patient Info"));
    else
        util_buzzer();
}

// [5차 판정 · 재론 금지] 1.0 과 같은 설계라 둔다: ③ 이동해 온 스코프를 또 이동시키면 2차 기록이 덮인다(조작 오류 범위)
//  ⑥ 알람 슬롯은 기기당 하나(뒤 스코프가 앞 알람을 덮음) ⑦ 이동+RTC 방전 복구가 1차 종료보다 앞설 수 있음
//  ⑧ 복구 추정의 세척 시간은 소독기 자신의 슬롯1(PC JSON 으로만 설정 — 이 추정 자체는 1.0 에 없고 09-23 결정)
//  ⑫ 일회성 ON 에서 담당자 미등록 종료는 담당자 0.
bool DisinfectionProcessor::disinfector_move(int deviceNumber, bool isMoved, DefaultRtc &rtc,
                                            const DateTime &startDt)
{
    // ★종료 기록을 먼저, 이동 커밋(Process)을 마지막에 — 반대면 종료 기록이 실패해도 태그가 '이동함' 으로
    //  굳어, 재접촉이 2호기 시작으로 넘어가 1호기 종료 시각을 영영 못 남긴다(프로젝트 "커밋은 마지막" 규칙).
    DisinfectionRecord record{deviceNumber, not_before_start(startDt, rtc.GetCurrentLocalDateTime())};
    if (!disinfection_end(isMoved, record)) return false;
    mCachedProcess.MovementNeeded = true;
    mCachedProcess.Rewrite        = 0;
    return write_process();
}

bool DisinfectionProcessor::disinfection_start(
    int deviceNumber, bool isMoved, bool isGuest, bool isRestart,
    const AlarmOption &alarmOption,
    DisinfectionOption &disinfectionOption, DefaultRtc &rtc)
{
    // 재시작(더블터치)은 커밋된 시작을 지우지 않는다 — 지운 뒤 중간에 실패하면 RW=2 인데 시작이 0 인 태그가
    // 남아 서버가 빈 시각을 등록한다. 세 블록과 자동 종료를 전부 다시 쓰므로 지울 이유도 없다.
    // [5차 판정 · 재론 금지] 성공 경로에선 아래 쓰기가 두 섹터를 전부 덮어 소거가 중복(≈100ms)이지만,
    //  중간 실패 시 지난 주기 기록이 새 시작과 섞여 남지 않게 하는 방어라 둔다(4차 B 도 유지 권고).
    if (!isMoved && !isRestart)
    {
        mScanner.ClearSector(5);
        mScanner.ClearSector(6);
    }

    // ★읽지 않는다 — DisinfectionDetail 은 정확히 10바이트(일시 8 + 그룹 2)이고 아래에서 둘 다 무조건
    //  덮어쓴다. 종전엔 읽기 실패가 시작을 취소했는데, 그 앞에서 이미 옛 기록을 지웠으므로
    //  "옛 기록 없음 + 새 시작 없음" 태그가 남았다. 성공 경로 바이트는 같다.
    DisinfectionDetail detail{};
    const uint8_t detailBlock = isMoved ? SECTOR14_DISINFECTION_DETAIL2 : SECTOR14_DISINFECTION_DETAIL;

    detail.GroupNumber = isGuest ? 2 : 1;

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

    const auto current = get_adjuest_start_time(rtc.GetCurrentDateTime(), rtc, alarmOption.GetTimeSlot1());
    if (current == DateTime{static_cast<uint32_t>(0)}) return false;   // [5차 판정 · 재론 금지] 호출자가 'Write Error' 로 알린다(실은 읽기) — 커밋 없음·조치 동일(다시 댐)

    // 방금 시계가 복구됐으면 미뤄 둔 교환일을 먼저 기록 — 이 스코프 태그에도 맞는 교환일이 들어가게.
    if (disinfectionOption.HasPendingClear() && !rtc.IsUnsynced())
        disinfectionOption.ApplyPendingClear(rtc.GetCurrentLocalDateTime());
    detail.DateTime = disinfectionOption.IsClearDateTimeEmpty()
        ? rtc.GetCurrentLocalDateTime()
        : disinfectionOption.GetClearDateTime();

    DisinfectionRecord record{
        deviceNumber,
        LocalDateTime{
            LocalDate{current.year(), current.month(), current.day()},
            LocalTime{current.hour(), current.minute(), current.second()}}};

    const uint8_t startBlk = isMoved ? SECTOR7_DISINFECTION_START : SECTOR5_DISINFECTION_START;
    const uint8_t keyBlk   = isMoved ? SECTOR7_DISINFECTION_START_MANAGER_KEY  : SECTOR5_DISINFECTION_START_MANAGER_KEY;
    const uint8_t nameBlk  = isMoved ? SECTOR7_DISINFECTION_START_MANAGER_NAME : SECTOR5_DISINFECTION_START_MANAGER_NAME;

    if (mScanner.Write(startBlk, &record, 10) != RfidResult::Ok) return false;
    if (!write_manager_key(keyBlk)) return false;     // 정본(RecordProcessor) — 세척과 같은 바이트
    if (!write_manager_name(nameBlk)) return false;
    if (mScanner.Write(detailBlock, &detail, 10) != RfidResult::Ok) return false;

    // ★미리 채우는 자동 종료도 **커밋 앞**에 둔다 — "종료 시각이 빌 수 없다"(사장님 09-23)가 안전망인데,
    //  커밋 뒤에 두면 실패해도 성공음이 나서 "시작은 있고 종료는 0" 인 태그가 조용히 나갔다.
    DisinfectionRecord autoEnd{record};
    autoEnd.DateTime = add_datetime(current, alarmOption.GetTimeSlot2(), record.DateTime.Time.Second);
    if (!disinfection_end(isMoved, autoEnd)) return false;

    // Process(소독 시작 플래그)는 **커밋** — 기록이 모두 성공한 뒤에 쓴다.
    // 앞에 두면 중간 실패 시 "소독했다"는 플래그만 서고 시작·종료 기록이 0 인
    // 태그가 남아, 서버가 3단 거부를 통과해 빈 시각을 등록한다
    // (1.0 승계 결함 — 2.2.5 수정). 세척 경로는 원래 이 순서였다.
    if (!write_process()) return false;

    // 더블터치 가드로 "시작"이 재실행된 경우에는 횟수를 다시 올리지 않는다
    // (2초 안에 두 번 대면 소독 1회에 횟수 2가 되던 것 — 2.2.5 수정).
    // [8차 판정 · 재론 금지] 커밋(위 write_process)이 확인 읽기에서 끊기면 여기까지 오지 못해 그 소독이
    //  횟수에서 빠지고, 태그엔 커밋이 남아 재접촉이 종료가 된다 → 액교환 주기가 1회 늦어진다(CC2 P3-2).
    //  순서를 바꾸면 **실패한 시작도 횟수를 올린다** — 그쪽이 더 나쁘다(횟수는 액교환 주기의 근거다).
    // [10차 판정 · 재론 금지] 같은 창에서 **횟수만 빠지는 게 아니다** — 호출자의 커밋 뒤 부수효과(host/guest
    //  슬롯·일회성 담당자 소모·알람)도 건너뛴다. 동시소독이면 시간창 안 둘째 스코프가 host 로 기록돼 **PC 대장에
    //  그룹0001 행이 둘**(한 배치가 2회)로 남는다(절단점 42 중 1자리 · EE2 실측).
    //  ★사장님 현장 사실(09-27): **동시소독을 쓰는 병원이 많다**(소독만 2개 동시 · 설정기 '동시소독 슬롯'=2).
    //   그래도 그대로 둔다 — 방향이 **과다 계수**(액교환이 일러진다 = 안전)이고, 슬롯 확보를 커밋 앞으로
    //   옮기면 나머지 **35자리**(절단점 43 중 커밋 3·슬롯 쓰기 앞 5 제외 · 11차 실측 정정)에 **유령 host**
    //   (과소 계수 = 오염된 액으로 계속 소독)가 생긴다. 리더는 카드가
    //   떠난 뒤 "커밋됐는지" 를 알 방법이 없어(확인 읽기도 실패) 국소적으로 복구할 정보가 없다.
    //   다시 볼 조건: 같은 소독기에서 **동시소독 창 안에 그룹0001 행이 둘**인 대장.
    //   ★11차 정정: 그 패턴은 **이 결함만 만드는 것이 아니다** — 배치 도중 그 소독기가 **재부팅**되면 RAM 의
    //    host·시간창이 사라져 창 안 둘째 스코프가 host 가 된다(FF2 P3-2 실측). 패턴을 보면 원인을 둘로
    //    나눠 봐야 한다(찢긴 커밋 / 재부팅). 게다가 "소독 20분" 전제도 설정값에 달렸다 — 동시소독 딜레이는
    //    **최대 120분**까지 둘 수 있어(DisinfectionOption 의 constrain) 20분을 넘겨 둔 현장이면 정상 배치 둘도
    //    같은 패턴을 낸다.
    // [11차 판정 · 재론 금지] 슬롯 판정은 **스코프 번호**로만 한다(`is_host(mCachedTag.Number)`) — 번호가 같은
    //  카드가 두 장 돌면(재발급 뒤 옛 카드 잔존·번호 중복 발급) 배치 셋째가 host 가 되어 횟수가 하나 더 오른다
    //  (과다 계수 = 액교환이 일러지는 안전한 방향 · FF2 P3-1). 번호 대신 UID 로 바꾸면 재발급한 카드가 남의
    //  슬롯을 못 물려받아 정상 이동·재발급 흐름이 깨진다 → 그대로 둔다. 근본 대책은 **번호 중복 발급을 막는 것**.
    if (!isGuest && !isRestart) disinfectionOption.IncrementCount();
    return true;
}

bool DisinfectionProcessor::disinfection_end(bool isMoved, DisinfectionRecord &record)
{
    const uint8_t recBlk  = isMoved ? SECTOR8_DISINFECTION_END : SECTOR6_DISINFECTION_END;
    const uint8_t keyBlk  = isMoved ? SECTOR8_DISINFECTION_END_MANAGER_KEY  : SECTOR6_DISINFECTION_END_MANAGER_KEY;
    const uint8_t nameBlk = isMoved ? SECTOR8_DISINFECTION_END_MANAGER_NAME : SECTOR6_DISINFECTION_END_MANAGER_NAME;

    // ★담당자를 먼저, **시각을 마지막에** — 반대면 담당자 블록만 실패했을 때 "오늘 종료 시각 + 지난
    //  주기 담당자" 쌍이 남는다(그 블록들은 서버 덤프도 안 지워 옛 담당자가 늘 남아 있다).
    if (!write_manager_key(keyBlk)) return false;     // 정본(RecordProcessor) — 세척과 같은 바이트
    if (!write_manager_name(nameBlk)) return false;
    return mScanner.Write(recBlk, &record, 10) == RfidResult::Ok;
}

DateTime DisinfectionProcessor::get_adjuest_start_time(DateTime current, DefaultRtc &rtc, int8_t washingMinutes)
{
    WashingRecord record{};
    if (mScanner.Read(SECTOR2_WASHING_START, &record, 10) != RfidResult::Ok)
        return DateTime{static_cast<uint32_t>(0)};

    const auto startTime = DefaultRtc::ToDateTime(record.DateTime);
    if (!startTime.isValid()) return current;   // 손상 기록(연도 0xFFFF 등)으로 RTC 를 2047 로 만들지 않는다
    // 세척 시작 시각이 현재 시각보다 늦으면 소독기 RTC가 초기화된 것으로 판단,
    // "세척 종료 시각 + 1분"으로 소독기 RTC를 복구하고 그 시각을 소독 시작
    // 시각으로 쓴다. (+1분 = 세척기→소독기 이동 시간 반영, 2026-08-09 사용자 확정.
    // 1.0은 세척 종료 시각 그대로였음 — 복구 동작 자체는 1.0과 동일)
    // [7차 사장님 판정 · 재론 금지] 이 복구는 **시계가 아직 안 맞춰진 소독기에만** 듣는다(`IsUnsynced`).
    //  5차엔 "동기된 소독기도 세척기를 따라간다(세척기가 유일한 운용 중 시각 기준)" 로 뒀는데, v2.2.21 부터 PC 가
    //  연결마다 시계를 맞추므로 기준이 둘이 됐고, 복구가 **PC 가 맞춘 시계를 되돌려** 둘이 번갈아 이겼다(BB2 P2-4).
    //  사장님 판정(09-27): **PC 를 우선**한다. 방전된 소독기는 PC 나 이 복구 둘 중 무엇으로든 살아나고,
    //  시계가 맞는 소독기는 세척기가 잘못 맞춰져 있어도 끌려가지 않는다.
    if (startTime >= current && rtc.IsUnsynced())
    {
        WashingRecord endRecord{};
        if (mScanner.Read(SECTOR3_WASHING_END, &endRecord, 10) != RfidResult::Ok)
            return DateTime{static_cast<uint32_t>(0)};
        // 종료 기록이 비었거나(0) 지난 주기면 "세척 시작 + 설정된 세척 시간" 으로 추정한다
        // (사장님 09-23). 0 에 +1분 하면 unixtime 이 돌아 RTC 가 2043년에 굳는다.
        auto base = DefaultRtc::ToDateTime(endRecord.DateTime);
        if (!base.isValid() || base < startTime)
            base = DefaultRtc::AddTimeSpan(startTime, washingMinutes, 0);
        const auto endTime = DefaultRtc::AddTimeSpan(base, 1, 0);
        rtc.SetDateTime(endTime);
        return endTime;
    }
    return current;
}
