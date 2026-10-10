#include "WashingProcessor.hpp"

void WashingProcessor::WashingProcess(int deviceNumber, const AlarmOption &alarmOption,
                                      const ManagerOption &managerOption, const RecordOption &recordOption,
                                      DefaultRtc &rtc, LcdPrinter &printer)
{
    if (is_valid(recordOption, managerOption, printer) <= 0) return;
    bool isEnd = (mCachedProcess.Rewrite == 1);

    if (!try_load_manager_data(managerOption, isEnd, recordOption.GetManagerDisposability(), printer))
        return;

    // 더블터치 = 태그에 적힌 시작이 2초 안(사장님 10-09 · kRestartWindowSec) — 종료가 아니라 시작 다시 하기(소독기와 같은 창). 소독기는 16차부터
    //  **같은 기기**에서만 재시작인데 세척기엔 기기 조건이 없다 — 창 안 다른 세척기 접촉이 그 세척기로 재시작되고 앞 세척기 알람은 남는다
    //  (17차 사실 · 세척기엔 Other Machine 관문이 없어 그대로 둔다).
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

// [5차 판정 · 재론 금지] Status==2 레거시 갈래의 Clear 셋(블록 8·9·5) 반환 무시 — `ClearSector(15)` 반환만 블록5 소거를
//  가르는 데 쓴다(15차 D). 2.0 은 Status 에 0·1 만 쓴다(델파이 2/3 기록 자리 없음).
void WashingProcessor::update_process(int deviceNumber)
{
    auto current = mCachedProcess.Status;
    if (current != 0 && current != 1)
    {
        uint8_t tempStatus = 0;
        if (current == 2)
        {
            // 1.0과 동일하나, 2.0의 섹터 캐시 덕에 같은 섹터 내 Clear는 인증 1회로 완료.
            // ★블록60 만 지우면 **블록61 이 덤프로 나갔다**(13차 HH1 P3-1) — 섹터15 데이터 3블록을 다 지운다.
            // ★[15차 사장님 B3 (가) · 재론 금지] 블록5(검사일시)는 **섹터15 가 지워졌을 때만·마지막에** — 덤프 소거·잔재 소거와 같은 순서(15차 III-B P3-3).
            //  섹터15 가 NACK 면 블록5 도 남아 그 주기 덤프 행에 옛 검사일시·본체번호가 한 번 실린다(지웠다면 빈칸) —
            //  이 커밋 뒤 태그는 WS=1 이라 잔재 판정을 안 타고, 덤프가 섹터15·블록5 를 지운다(16차 IV-B 정정).
            //  접촉이 끊기면 Status 2 가 커밋 전이라 남아 재접촉이 처음부터 다시 지운다.
            const bool subjectsCleared = mScanner.ClearSector(15) == RfidResult::Ok;
            mScanner.Clear(SECTOR2_PATIENT_KEY);
            mScanner.Clear(SECTOR2_PATIENT_NAME);
            if (subjectsCleared) mScanner.Clear(SECTOR1_GATEWAY);
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
    //   **커밋된 이번 주기 세척 시작이 사라진다**(소독기·서버가 그 주기를 거부한다). 소독 시작엔 선소거 자체가
    //   없다(15차 A4).
    //  [10차 판정] 대가: 아직 서버에 안 올린 **지난 주기**는 이 선행 쓰기 시점(접촉 초반)에 덤프 불가가 된다
    //   — 종전엔 커밋(접촉 끝)까지 살았다. 성공한 세척 시작도 그 주기를 어차피 덮으므로 잃는 것은
    //   "찢긴 시도에서 몇 초 일찍" 뿐이고, 안 내리면 **안 한 소독이 완료로 대장에 남는다**(9회차 P1).
    const uint8_t prevStatus = mCachedProcess.Status;
    if (!isRestart)
    {
        // 완료 처리가 지우다 끊긴 검사 잔재(블록5 검사일시·본체번호 · 섹터15 검사항목)를 비운다 — 안 지우면 옛 검사항목이
        //  다음 주기 덤프에 실렸다(12차 GG1 P3-5).
        // ★[사장님 선택 09-28 · 재론 금지] 판정은 **표지로만** 한다(시간 비교 없음):
        //  공정 전부 0(완료 처리됨) + Status 0(환자 없음 — 완료 커밋이 내렸거나 폴백) + 검사일시 있음 → 지운다.
        //  · 덤프 전(공정 ≠ 0)은 절대 안 지운다 — 블록5 는 게이트웨이만 쓰고 게이트웨이는 WS≠0 을 거부하므로 그 검사는
        //    진행 중 주기의 것이다(소독 뒤 재세척이 그것을 지웠던 것이 14차 II-B P1).
        //  · Status 1(환자 있음)은 게이트웨이가 완료 뒤에 새로 쓴 검사 → 남긴다. 2·3 은 레거시(update_process) · >3 은 0 과
        //    같이 지운다(아래 A6).
        //  · 대가(사장님 감수): 완료 뒤 **환자 없이(폴백)** 받은 검사는 여기서 지워진다 — 블록5 전체(본체번호·검사일시)와
        //    섹터15 라, 세척관리 대장엔 본체번호 0000 · 검사종류 빈칸 · 검사날짜 = 소독일로 나간다.
        //  · 12·13·14차에 시간(블록10→14→24 · 관계)으로 가르려 한 것이 세 번 결함이 됐다 — "게이트웨이·세척기·소독기를
        //    거쳤는지는 표지에 있다, 왜 시간으로 가르나"(사장님). 찢긴 시도 표지도 필요 없다(재접촉이 같은 표지를 다시 본다).
        // ★소거 순서: 섹터15 먼저, 판정 근거인 블록5 는 **마지막·섹터15 가 지워졌을 때만** — 먼저 지우면 다음 접촉이
        //  Year==0 으로 건너뛰어 남은 검사항목이 그 주기 덤프 행에 한 번 실린다(덤프가 지운다 · HH1 ⑤ · 16차 정정).
        //  소거 실패로 세척 시작을 막지는 않는다(HH1 P2-1). [16차 판정] 블록5 읽기 실패도 같은 방향으로 '잔재 없음' 으로
        //  지나간다(형제 started_just_now 와 달리 멈추지 않는다 — 남은 잔재는 그 주기 덤프에 한 번 실린다).
        // [15차 사장님 A9 · 재론 금지] `afterDump` 관문은 둔다 — 첫 세척의 섹터15 소거가 NACK 로 실패한 태그는 같은 주기
        //  재세척에서 다시 지우지 않는다(덤프 전은 절대 안 지운다 · 잃는 것은 찢긴 잔재 1주기). 14차 "도달 차이 없음" 은 이 경우를
        //  못 봤다(15차 III-B P3-1 · III-H k2).
        //  예외(위 "같은 표지" 도): 덤프 전 재세척의 선행 쓰기(공정 0) 직후 끊기면 재접촉의 afterDump 가 참 — 지워지는 것은
        //  Status 0(·>3) 검사뿐이라 무해(16차 IV-I ④).
        // [15차 사장님 A6 · 재론 금지] Status 0 과 **모르는 값(>3)** 둘 다 — 손상 Status 태그는 환자 블록만 비우고 Status 0 으로 커밋되므로
        //  검사도 같이 비워야 환자 없는 행에 옛 검사가 실리지 않는다(11차 FF② 갈래와 같은 짝).
        const bool afterDump = mCachedProcess.WashingStatus == 0 && mCachedProcess.DisinfectionStatus == 0 &&
                               mCachedProcess.DisinfectionCount == 0 && mCachedProcess.MachineNumber == 0;
        WashingRecord gw{};       // 블록5 = {int 본체번호, LocalDateTime 검사일시} — 레코드와 같은 10바이트
        if (afterDump && (prevStatus == 0 || prevStatus > 3) &&
            mScanner.Read(SECTOR1_GATEWAY, &gw, 10) == RfidResult::Ok &&
            gw.DateTime.Date.Year != 0)
        {
            if (mScanner.ClearSector(15) == RfidResult::Ok)
                (void)mScanner.Clear(SECTOR1_GATEWAY);
        }

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
    // ★담당자를 먼저, **시각을 마지막에** — 반대면 담당자 블록만 실패했을 때 "새 종료 시각 + 옛 담당자" 쌍이
    //  남는다(옛 담당자 = 종료 접촉이면 이번 주기 시작이 미리 채운 담당자 · 미리채움이면 지난 주기 담당자).
    if (!write_manager_key(SECTOR4_WASHING_END_MANAGER_KEY)) return false;
    if (!write_manager_name(SECTOR4_WASHING_END_MANAGER_NAME)) return false;
    return mScanner.Write(SECTOR3_WASHING_END, &record, 10) == RfidResult::Ok;
}
