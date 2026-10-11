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
    /// 이 기기에서 이동이 Write Error 로 끝난 스코프(RAM · 한 칸) — 커밋은 닿았는데 확인이 끊긴 재접촉을 재확인으로 가른다(16차 A2).
    /// 표지가 사라지는 길(18차에 넷으로): ① 그 스코프의 이동/종료 **성공**(17차 — 커밋 전 실패 뒤 재접촉 이동이 성공해도 표지가 남아
    /// 10초 안 되넣기(A→A 2차 시작)를 삼켰다) ② **재확인**하면 2초만 더 산다(shorten — 재확인 직후의 튐도 재확인이 되게 · 성공한 이동의
    /// 섹터6 2초와 같은 규칙) ③ 10초 만료 — 소독기 접촉마다 expire 가 실제로 지운다(시각만 비교하면 millis 한 바퀴 49.7일 뒤 같은
    /// 10초 띠에서 되살아났다) ④ 재부팅. 다른 스코프의 표지는 안 지운다. 번호 −1(손상 태그)은 빈 표지와 같은 값이라 거른다.
    void note_write_error(int16_t scope) { mFailScope = scope; mFailMs = millis(); }
    bool failed_just_now(int16_t scope) const
    {
        return mFailScope >= 0 && mFailScope == scope && (millis() - mFailMs) < kFailWindowMs;
    }
    void clear_write_error(int16_t scope) { if (mFailScope == scope) mFailScope = -1; }
    void expire_write_error() { if (mFailScope >= 0 && (millis() - mFailMs) >= kFailWindowMs) mFailScope = -1; }
    void shorten_write_error(int16_t scope)
    {
        if (mFailScope == scope) mFailMs = millis() - (kFailWindowMs - kReconfirmWindowSec * 1000UL);
    }
    static constexpr uint32_t kFailWindowMs{10000UL};
    /// 더블터치 재시작 창(초) — **2초**(사장님 10-09 · 15차 A3 의 10초를 되돌림). 이동 재확인의 섹터6 시각 창도 2초(튐 방지 · 따로 둔 상수).
    static constexpr uint8_t kRestartWindowSec{2};
    static constexpr uint8_t kReconfirmWindowSec{2};
    bool hasnt_patient_info(const RecordOption &recordOption);

    /// 더블터치 판정 — 태그의 그 블록에 적힌 시각이 지난 windowSec 초 안(0 ≤ 경과 < windowSec · 기본 kRestartWindowSec)인가.
    /// 태그가 말해 주므로 RAM 에 마지막 시작을 기억할 필요가 없다(재부팅·기록 실패 뒤에도 옳다). 시작 블록(재시작 판정)과
    /// 섹터6 종료 블록(이동 직후 튐 판정) 둘에 쓴다. 1 = 창 안 · 0 = 아니다 · -1 = 그 블록을 못 읽음(판정 불가 — 호출자가
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
