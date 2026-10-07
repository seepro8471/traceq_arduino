#pragma once

#include "BaseProcessor.hpp"
#include "TraceQ_Arduino/data/WashingRecord.hpp"
#include "TraceQ_Arduino/data/nvm/ManagerOption.hpp"
#include "TraceQ_Arduino/data/nvm/RecordOption.hpp"
#include "TraceQ_Arduino/module/rtc/DefaultRtc.hpp"

class RecordProcessor : public BaseProcessor
{
public:
    void SaveManagerData(const RecordOption &recordOption, ManagerOption &managerOption, LcdPrinter &printer);

protected:
    RecordProcessor(Tag &tag, TagSerial &tagSerial, Process &process, RfidController &scanner)
        : BaseProcessor(tag, tagSerial, process, scanner) {}

    /// 1 = 정상 · 0 = 거부(담당자 없음 — 알림 냄) · -1 = 읽기 실패(알림 냄). 호출자는 <= 0 이면 나간다 —
    /// 소독기는 어느 쪽에도 이동 표시를 내리지 않는다(15차 사장님 A1).
    int8_t is_valid(const RecordOption &recordOption, const ManagerOption &managerOption, LcdPrinter &printer);
    bool write_manager_key(uint8_t addr);
    bool write_manager_name(uint8_t addr);
    bool try_load_manager_data(const ManagerOption &managerOption, bool isEnd, bool disposability, LcdPrinter &printer);
    /// 일회성 담당자 소모 — 시작 커밋이 성공한 뒤에만 부른다.
    void consume_disposability() { mDisposabilityFlag = false; }
    /// 메뉴에서 일회성 설정이 바뀌면 표지를 버린다(15차 사장님 A7) — 파생 클래스가 `using` 으로 공개한다.
    void ResetDisposability() { mDisposabilityFlag = false; }
    /// 이 기기에서 이동이 Write Error 로 끝난 스코프(RAM · 10초) — 커밋은 닿았는데 확인이 끊긴 재접촉을 재확인으로 가른다(16차 A2).
    /// 표지는 **재확인 · 그 스코프의 이동/종료 성공 · 10초** 중 먼저 오는 것으로 사라진다 — 17차: 커밋 전 실패 뒤 재접촉 이동이
    /// 성공해도 표지가 남아 10초 안 되넣기(A→A 2차 시작)를 재확인으로 삼켰다(7갈래 독립 발견). 한 칸뿐이라 다른 스코프의 표지는
    /// 안 지운다. 번호 −1(손상 태그)은 빈 표지와 같은 값이라 거른다.
    void note_write_error(int16_t scope) { mFailScope = scope; mFailMs = millis(); }
    bool failed_just_now(int16_t scope) const
    {
        return mFailScope >= 0 && mFailScope == scope && (millis() - mFailMs) < kFailWindowMs;
    }
    void clear_write_error(int16_t scope) { if (mFailScope == scope) mFailScope = -1; }
    static constexpr uint32_t kFailWindowMs{10000UL};
    /// 더블터치 재시작 창(초) — 15차 사장님 A3: 2→10. 이동 재확인의 섹터6 시각 창은 따로 2초(튐 방지 — 재확인은 같은 기기만이라
    /// 넓히면 A→A 의 성공한 이동 뒤 빠른 되넣기(2차 시작)를 삼킨다 · 찢긴 이동은 위 RAM 실패 표지 10초가 잡는다 · 16차).
    static constexpr uint8_t kRestartWindowSec{10};
    static constexpr uint8_t kReconfirmWindowSec{2};
    bool hasnt_patient_info(const RecordOption &recordOption);

    /// 더블터치 판정 — 태그에 적힌 시작 시각이 지난 windowSec 초 안(0 ≤ 경과 < windowSec · 기본 10)이면 "방금 시작한 그 태그" 다.
    /// 태그가 말해 주므로 RAM 에 마지막 시작을 기억할 필요가 없다(재부팅·기록 실패 뒤에도 옳다).
    /// 더블터치 판정. 1 = 시작이 지난 windowSec 초 안(재시작) · 0 = 아니다(종료) · -1 = 시작 블록을 못 읽음(판정 불가 — 호출자가
    /// Read Error 로 알리고 아무것도 바꾸지 않는다). 종전엔 못 읽으면 조용히 0 이 되어 1초짜리 종료가 기록됐다(5차 V4).
    /// startOut: 이 판정이 읽은 시작 시각을 그대로 넘겨준다(종료 보정이 다시 읽지 않도록 · 카드 동작 1회 절약).
    int8_t started_just_now(uint8_t startBlock, DefaultRtc &rtc, DateTime *startOut = nullptr,
                            uint8_t windowSec = kRestartWindowSec);

    /// 종료 시각이 태그의 시작보다 앞서면 그 시작 시각을 돌려준다 — 시계를 뒤로 돌린 뒤의 "종료 < 시작"
    /// 기록을 막는다. start 는 started_just_now 가 **이미 읽은** 값이다(다시 읽으면 그 읽기가 실패할 때
    /// 보정이 조용히 꺼졌다 · 9차 DD1).
    static LocalDateTime not_before_start(const DateTime &start, const LocalDateTime &end);

    static LocalDateTime add_datetime(const DateTime &current, uint8_t minute, uint8_t seconds);

private:
    void load_manager_data(const ManagerOption &managerOption);

    bool mDisposabilityFlag{false};
    int16_t  mFailScope{-1};   // 직전 Write Error 스코프 번호(없으면 -1)
    uint32_t mFailMs{0};
};
