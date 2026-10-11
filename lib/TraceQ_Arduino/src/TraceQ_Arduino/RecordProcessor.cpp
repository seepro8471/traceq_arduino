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

    // Tag.ID는 14바이트인데 SetData 는 KEY_SIZE(16)바이트를 복사한다 —
    // 1.0은 인접 2바이트를 함께 읽는 OOB였음. 16바이트 버퍼로 0패딩 후 전달.
    unsigned char key[ManagerOption::KEY_SIZE]{};
    memcpy(key, mCachedTag.ID, sizeof(mCachedTag.ID));
    managerOption.SetData(key, mCachedTagSerial.Serial);

    if (recordOption.GetManagerDisposability()) mDisposabilityFlag = true;

    // Tag.ID[14]는 구조체 마지막 멤버라 14바이트가 다 차면 종료 NUL 이 없다 —
    // 그대로 출력하면 인접 전역 영역을 계속 읽는다 (2.2.5).
    char idText[sizeof(mCachedTag.ID) + 1]{};
    memcpy(idText, mCachedTag.ID, sizeof(mCachedTag.ID));
    // [13차 HH2 P3-2] 소리 100×1(형제 성공음은 50×1) · ID 글자는 **200ms** 만 보여 사람이 못 읽는다(1.0 승계).
    //  읽히게 하려면 500 이지만 소리가 500×1 로 바뀐다 — [15차 사장님께 물음(09-28) · 그대로 · 재론 금지](C2). 소리는 t_hh2lock L0 이 잠근다.
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
        // [18차 판정 · 사장님이 뒤집을 수 있음] 시작 커밋은 닿고 확인만 끊긴 접촉(시작 이탈 지점 약 30 중 1자리) 뒤, 재시작 창(2초)을 넘겨
        //  다시 대면 종료로 처리돼 이 표지가 남는다 → 다음 스코프 하나가 담당자 접촉 없이 시작된다(세척기·소독기 둘 다 실측). 남는 표지는
        //  방금 그 스코프를 대던 **그 담당자**의 것이라 둔다. 닫으려면 "커밋을 시도하면 소모"(실패 뒤 재접촉이 No Manager Info 가 된다 —
        //  5차가 고친 불편)나 시작 실패 표지(상태 추가)가 필요하다. 16차 문구 "드문 경우" 는 10초 창 기준이었다.
    }
    load_manager_data(managerOption);
    return true;
}

bool RecordProcessor::hasnt_patient_info(const RecordOption &recordOption)
{
    return recordOption.GetPatientCheck() && mCachedProcess.Status != 1;
}

int8_t RecordProcessor::started_just_now(uint8_t startBlock, DefaultRtc &rtc, DateTime *startOut, uint8_t windowSec)
{
    WashingRecord record{};   // 세척·소독 시작·종료 기록은 레이아웃이 같다(번호 2 + 일시 8)
    if (mScanner.Read(startBlock, &record, 10) != RfidResult::Ok) return -1;   // 판정 불가 — '종료' 로 떨어뜨리지 않는다
    const auto started = DefaultRtc::ToDateTime(record.DateTime);
    if (startOut != nullptr) *startOut = started;   // 종료 보정이 이 읽기를 그대로 쓴다
    if (!started.isValid()) return 0;
    const int32_t gap = (rtc.GetCurrentDateTime() - started).totalseconds();
    // ★[사장님 10-09 · 재론 금지] 재시작 창은 **2초**(kRestartWindowSec). 15차 A3 가 10초로 넓혔던 것을 사장님이 2초로 되돌리셨다
    //  ("10초 안 다시 댐을 2초 안으로"). 대가가 닿는 길은 하나 — **시작 커밋은 태그에 닿았는데 확인 읽기가 끊겨 실패음이 난 접촉**
    //  (시작 이탈 지점 약 30 중 1자리 · 나머지 실패는 태그가 '시작 전' 그대로라 다시 대면 창과 무관하게 새 시작이다 · 18차 실측).
    //  그때 실패음(0.8초)이 초 단위 창을 먼저 먹어 재접촉은 대개 '종료' 다: 성공음 · 2~4초짜리 기록(미리 채운 자동 종료가 그 시각으로
    //  덮임 — 진짜 종료 접촉이 다시 덮는다) · 그 알람 없음 · 소독 횟수 −1 · 동시소독이면 다음 스코프도 그룹1 · 일회성 담당자 표지 잔존.
    //  v2.2.29~32 와 같은 행위다. 이동 재확인(A2)은 별개 — RAM 실패 표지 10초 또는 섹터6 시각 2초.
    // ★창은 뒤쪽으로만 본다 — 시계를 뒤로 돌리면 태그의 시작이 '미래' 가 되는데, 대칭 창이면
    //  종료 터치가 '시작 재실행' 이 되어 실제 종료 시각이 사라졌다(BB2 P3-1).
    return gap >= 0 && gap < windowSec;
}

LocalDateTime RecordProcessor::not_before_start(const DateTime &s, const LocalDateTime &end)
{
    // 시계를 뒤로 돌린 뒤(설정기 JSON·PC 의 T·기기 메뉴) 종료를 대면 "종료 < 시작" 기록이 남아 PC 대장에
    // 음수 시간이 찍혔다(BB2 P2-2). 시작보다 앞선 종료는 시작 시각으로 끌어올린다 — 기록을 잃지는 않는다.
    // 시작 시각은 더블터치 판정이 이미 읽은 값이다 — 다시 읽으면 그 읽기가 실패할 때 보정이 조용히 꺼졌다.
    if (!s.isValid()) return end;
    const DateTime e{end.Date.Year, end.Date.Month, end.Date.Day,
                     end.Time.Hour, end.Time.Minute, end.Time.Second};
    if (!e.isValid() || e.unixtime() >= s.unixtime()) return end;
    return LocalDateTime{LocalDate{s.year(), s.month(), s.day()},
                         LocalTime{s.hour(), s.minute(), s.second()}};
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
    // ★표지를 먼저 본다 — SetData 가 표지를 먼저 내리므로 '담당자 없음' 인데도 칸에는 반쪽(새 키+옛 이름)이
    //  남을 수 있고, 종료 터치는 표지를 안 보고 그대로 기록했다(Z1 P3-2).
    if (!managerOption.HasData())
    {
        memset(mCachedTag.ID, 0, sizeof(mCachedTag.ID));
        memset(mCachedTagSerial.Serial, 0, sizeof(mCachedTagSerial.Serial));
        return;
    }
    managerOption.GetKey(mCachedTag.ID, 14);
    managerOption.GetName(mCachedTagSerial.Serial, 16);
}
