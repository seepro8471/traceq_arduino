#pragma once

#include "TraceQ_Arduino/version.hpp"
#include "TraceQ_Arduino/ui/LcdPrinter.hpp"
#include "TraceQ_Arduino/module/rtc/DefaultRtc.hpp"

/**
 * \class ReaderUserInterface
 * \extends AbstractUserInterface
 * \brief READER_MODE를 위해 최소한의 기능만을 지원하는 UserInterface.
 *
 * \since 1.2
 */
class ReaderUserInterface : public LcdPrinter
{

public:
    explicit ReaderUserInterface(DisplayClass &lcd) : LcdPrinter(lcd){};

    void UserInterfaceInitialize(char deviceType) override;

    /**
     * \brief Home 화면을 표시한다.
     *
     * \param rtc 현재 시간을 출력하기 위한 RtcImpl
     * \param deviceNumber 현재 기기의 번호
     */
    void DisplayHome(DefaultRtc &rtc, int deviceNumber);

protected:
private:
    /**
     * \brief 현재 시각을 출력하기 위한 버퍼. Home 화면 구성에 사용된다.
     */
    char mDateTimeBuffer[20]{};

    /**
     * \brief 현재 기기의 타입과 번호를 출력하기 위한 버퍼. Home 화면 구성에 사용된다.
     *
     * \details col 16, row 3에 표시된다.
     */
    char mDeviceInfoBuffer[5]{};
};
