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
    sprintf(mDeviceInfoBuffer, "%c:%02d", mType, deviceNumber);
    mLcd.setCursor(16, 3);
    mLcd.print(mDeviceInfoBuffer);
}
