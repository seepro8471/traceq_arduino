#pragma once

#include "RecordProcessor.hpp"
#include "TraceQ_Arduino/data/DisinfectionRecord.hpp"
#include "TraceQ_Arduino/data/WashingRecord.hpp"
#include "TraceQ_Arduino/ui/UserInterface.hpp"

class DisinfectionProcessor : protected RecordProcessor
{
public:
    DisinfectionProcessor(Tag &tag, TagSerial &tagSerial, Process &process, RfidController &scanner)
        : RecordProcessor(tag, tagSerial, process, scanner) {}

    // 소독기(D)의 매니저 태그 저장은 이 인스턴스의 SaveManagerData를 써야
    // 일회성 플래그(mDisposabilityFlag)가 소독 과정과 같은 곳에 충전된다.
    // (1.0은 washingProcessor에 저장하는 인스턴스 불일치 버그가 있었음)
    using RecordProcessor::SaveManagerData;

    void DisinfectionProcess(int deviceNumber, const AlarmOption &alarmOption,
                             DisinfectionOption &disinfectionOption, const ManagerOption &managerOption,
                             const RecordOption &recordOption, DefaultRtc &rtc, LcdPrinter &printer);

    void SetMovable() { mMovable = true; }

protected:
    void disinfector_move(int deviceNumber, bool isMoved, DefaultRtc &rtc);
    void disinfection_start(int deviceNumber, bool isMoved, bool isGuest, const AlarmOption &alarmOption,
                            DisinfectionOption &disinfectionOption, DefaultRtc &rtc);
    void disinfection_end(bool isMoved, DisinfectionRecord &record);

private:
    bool is_host(uint8_t n) const  { return mHostScopeNumber == n; }
    bool is_guest(uint8_t n) const { return mGuestScopeNumber == n; }
    void set_host(uint8_t n, const DateTime &now) { mHostScopeNumber = n; mStartTime = now; }
    void set_guest(uint8_t n) { mGuestScopeNumber = n; mStartTime = DateTime{}; }

    /**
     * \brief 소독 시작 시각 보정.
     *
     * 태그의 세척 시작 시각이 현재 시각보다 늦으면 소독기 RTC가 초기화된 것으로
     * 판단하고, "세척 종료 시각 + 1분"으로 소독기 RTC를 복구한 뒤 그 시각을
     * 반환한다. RTC 자동 복구는 1.0과 동일하며, +1분(이동 시간 반영)은
     * 2026-08-09 사용자 확정 사양. (2.0 초기 재작성에서는 RTC 복구가 소실되고
     * +3분 가산만 있었음.)
     *
     * \return 보정된 시각. 태그 읽기 실패 시 DateTime{0} (호출측에서 중단).
     */
    DateTime get_adjuest_start_time(DateTime current, DefaultRtc &rtc);

    DateTime mStartTime{};
    uint8_t  mHostScopeNumber  = static_cast<uint8_t>(-1);
    uint8_t  mGuestScopeNumber = static_cast<uint8_t>(-1);
    bool     mMovable{false};
};
