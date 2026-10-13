#include "ReaderUserInterface.hpp"

void ReaderUserInterface::UserInterfaceInitialize(const char deviceType)
{
    mType = deviceType;
    mLcd.init();
    mLcd.backlight();
}

void ReaderUserInterface::DisplayHome(DefaultRtc &rtc, const int deviceNumber)
{
    // date time
    mLcd.setCursor(0, 0);
    mLcd.print(rtc.ToInternalString(mDateTimeBuffer));

    // version
    mLcd.setCursor(0, 3);
    mLcd.print(TRACEQ_ARDUINO_VERSION);

    // device
    snprintf(mDeviceInfoBuffer, sizeof(mDeviceInfoBuffer), "%c:%02d", mType, deviceNumber);   // 버퍼 [5] — 번호 100 이상이면 잘린다(넘치지 않게)
    mLcd.setCursor(16, 3);
    mLcd.print(mDeviceInfoBuffer);
}
