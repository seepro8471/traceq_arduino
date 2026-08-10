#pragma once

#include "TraceQ_Arduino/data/PinMap.hpp"
#include "TraceQ_Arduino/module/lcd/Menu.hpp"
#include "TraceQ_Arduino/avr/AvrUtil.hpp"

/**
 * \class LcdPrinter
 * \brief Lcd에 문자열을 표시하는 기능을 구현하는 추상 클래스.
 *
 * \since 1.2
 */
class LcdPrinter
{

public:
    /**
     * \brief 현재 기기의 타입을 기준으로 UserInterface를 초기화한다.
     *
     * \param deviceType 현재 기기의 타입
     */
    virtual void UserInterfaceInitialize(char deviceType) = 0;

    /**
     * \brief 현재 스크린을 지운다.
     *
     * \note inline function.
     */
    inline void ClearScreen()
    {
        mLcd.clear();
    }

    /**
     * \brief 지정된 위치에 문자열을 출력한다.
     *
     * \param col col position
     * \param row row position
     * \param string 출력할 문자열
     */
    void Info(uint8_t col, uint8_t row, const __FlashStringHelper *string);

    /**
     * \brief 지정된 위치에 일정 시간 동안 문자열을 출력한다.
     *
     * \param col col position
     * \param row row position
     * \param ms delay milliseconds
     * \param string 출력할 문자열
     */
    void InfoForWhile(uint8_t col, uint8_t row, unsigned long ms, const __FlashStringHelper *string);

    /**
     * \brief 지정된 위치에 문자열을 출력한다.
     *
     * \param col col position
     * \param row row position
     * \param string 출력할 문자열
     */
    void Info_cstr(uint8_t col, uint8_t row, const char *string);

    /**
     * \brief 지정된 위치에 일정 시간 동안 문자열을 출력한다.
     *
     * \param col col position
     * \param row row position
     * \param ms delay milliseconds
     * \param string 출력할 문자열
     */
    void InfoForWhile_cstr(uint8_t col, uint8_t row, unsigned long ms, const char *string);

    /**
     * \brief 지정된 위치에 일정 시간 동안 buzzer를 울리며 문자열을 출력한다.
     *
     * \param col col position
     * \param row row position
     * \param ms delay milliseconds
     * \param string 출력할 문자열
     */
    void Notify(uint8_t col, uint8_t row, unsigned long ms, const __FlashStringHelper *string);

    /**
     * \brief 지정된 위치에 일정 시간 동안 buzzer를 울리며 문자열을 출력한다.
     *
     * \param col col position
     * \param row row position
     * \param ms delay milliseconds
     * \param string 출력할 문자열
     */
    void Notify_cstr(uint8_t col, uint8_t row, unsigned long ms, const char *string);

    /**
     * \brief 지정된 위치에 warning 문자열을 출력하고 buzzer와 led를 사용해 알린다.
     *
     * \param col col position
     * \param row row position
     * \param string 출력할 문자열
     */
    void Warning(uint8_t col, uint8_t row, const __FlashStringHelper *string);

    /**
     * \brief 지정된 위치에 warning 문자열을 출력하고 buzzer와 led를 사용해 알린다.
     *
     * \param col col position
     * \param row row position
     * \param string 출력할 문자열
     */
    void Warning_cstr(uint8_t col, uint8_t row, const char *string);

    /**
     *
     * \param col col position
     * \param row row position
     * \param ms delay milliseconds
     * \param count loop count
     * \param string 출력할 문자열
     */
    void CustomWarning(uint8_t col, uint8_t row, unsigned long ms, uint8_t count, const __FlashStringHelper *string);

    /**
     *
     * \param col col position
     * \param row row position
     * \param ms delay milliseconds
     * \param count loop count
     * \param string 출력할 문자열
     */
    void CustomWarning_cstr(uint8_t col, uint8_t row, unsigned long ms, uint8_t count, const char *string);

    /**
     * \brief 지정된 위치에 문자열을 출력하고 buzzer와 led를 사용해 알린다.
     *
     * \param col col position
     * \param row row position
     * \param string 출력할 문자열
     */
    void Debug(uint8_t col, uint8_t row, const __FlashStringHelper *string);

    /**
     * \brief 지정된 위치에 debug 문자열을 출력하고 buzzer와 led를 사용해 알린다.
     *
     * \param col 출력할 col
     * \param row 출력할 row
     * \param string 출력할 문자열
     */
    void Debug_cstr(uint8_t col, uint8_t row, const char *string);

    void CustomDebug(uint8_t col, uint8_t row, unsigned long ms, uint8_t count, const __FlashStringHelper *string);

    void CustomDebug_cstr(uint8_t col, uint8_t row, unsigned long ms, uint8_t count, const char *string);

protected:
    explicit LcdPrinter(DisplayClass &lcd) : mLcd(lcd){};

    /**
     * \brief row의 문자열을 지운다.
     *
     * \param row cursor row
     */
    void clear_line(uint8_t row);

    /**
     * \brief 지정된 위치에 문자열을 출력하고 buzzer와 led를 blink한다.
     *
     * \param col 출력할 col
     * \param row 출력할 row
     * \param count blink count
     * \param string 출력할 문자열
     */
    void print_and_blink(uint8_t col, uint8_t row, uint8_t count, const __FlashStringHelper *string);

    /**
     *
     * \param col col position
     * \param row row position
     * \param ms delay milliseconds
     * \param count loop count
     * \param string 출력할 문자열
     */
    void print_and_notify(uint8_t col, uint8_t row, unsigned long ms, uint8_t count, const __FlashStringHelper *string);

    /**
     *
     * \param col col position
     * \param row col position
     * \param ms delay milliseconds
     * \param count loop count
     * \param string 출력할 문자열
     */
    void print_and_notify_cstr(uint8_t col, uint8_t row, unsigned long ms, uint8_t count, const char *string);

    /**
     * \brief parameter 'line'을/를 출력한다.
     *
     * \param line 출력할 line
     */
    void print_line(const Line &line);

    /**
     * \brief parameter 'screen'을/를 출력한다.
     *
     * \param screen 출력할 screen
     */
    void print_screen(const Screen &screen);

    /**
     * \brief parameter 'screen'에 속한 line 중 index가 beggin 이상 end 이하인 line만을 출력한다.
     *
     * \param screen 출력할 screen
     * \param beggin 출력할 line의 beggin index
     * \param end 출력할 line의 end index
     */
    void print_sceen_by_position(const Screen &screen, uint8_t beggin, uint8_t end);

protected:
    /**
     * \brief 현재 기기의 타입.
     *
     * \details 이 데이터는 UserInterfaceInitialize 함수에서 저장된다. 생성자로 받을 수 있었다면 좋았겠지만
     * 그렇게하기 위해서는 이 클래스를 setup()에서 DeviceOption의 초기화가 끝난 다음에 생성해야 하므로 무리가 있다.
     */
    char mType{};

    /**
     * \brief 실제 LCD Screen을 조정하기 위한 클래스 인스턴스.
     *
     * \warning 클래스 내부에서 초기화할 경우 화면에 이상이 생기는 버그가 발생하므로 반드시 생성자를 통해 초기화 할 것.
     */
    DisplayClass &mLcd;
};
