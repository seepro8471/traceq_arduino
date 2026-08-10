#pragma once

#include "TraceQ_Arduino/ui/LcdPrinter.hpp"

/**
 * \class BaseUserInterface
 * \extends AbstractUserInterface
 * \brief 컨텐츠를 강조하고 표시하는 기능을 구현하는 추상 클래스.
 *
 * \since 1.2
 */
class BaseUserInterface : public LcdPrinter
{

protected:
    /**
     * \enum Position
     */
    enum class Position
    {

        Equal = 0,

        Lower1,

        Lower2,

        LowerN,

    };

    /**
     * \brief BaseUserInterface 클래스의 생성자. protected modifier를 가지므로 상속받은 생성자에서 호출된다.
     *
     * \param lcd DisplayClass instance
     */
    explicit BaseUserInterface(DisplayClass &lcd) : LcdPrinter(lcd){};

    // get/setter

    inline uint8_t getCurrentPosition() const
    {
        return mCurrentPosition;
    }

    inline void setCurrentPosition(const uint8_t position)
    {
        mCurrentPosition = position;
    }

    inline uint8_t getMaximumPosition() const
    {
        return mMaximumPosition;
    }

    inline void setMaximumPosition(uint8_t position)
    {
        mMaximumPosition = position;
    }

    // util

    /**
     * \brief 현재 위치(current position)와 최대 위치(maximum position)을/를 비교해 그 결과를 반환한다.
     *
     * \details 자세한 내용은 구현한 코드를 참고할 것.
     *
     * \return 현재 위치(current position)와 최대 위치(maximum position)의 비교 결과
     */
    Position compare_position() const;

    /**
     * \brief 현재 위치(current position)을/를 조정한다.
     *
     * \param decrement 이 값이 true인 경우 현재 위치를 감소시키고, false인 경우 증가시킨다
     */
    void adjust_position(bool decrement = false);

    // print

    /**
     * \brief 지정된 위치(0, 0)에 제목을 출력한다.
     *
     * \param title 제목 문자열
     */
    void print_title(const char *title);

    /**
     * \brief 지정된 위치(0, 0)에 제목을 출력한다.
     *
     * \param title 제목 문자열
     */
    void print_title(const __FlashStringHelper *title);

    /**
     * \brief option navigation을 출력하고 강조한다.
     */
    void print_option_nagivation();

    /**
     * \brief option navigation에 속한 line 중 index가 beggin 이상 end 이하인 line만을 출력한다.
     *
     * \param beggin 출력할 line의 beggin index
     * \param end 출력할 line의 end index
     */
    void print_option_navigation(uint8_t beggin, uint8_t end);

    /**
     * \brief menu navigation을 출력한다.
     */
    void print_menu_navigation();

    // emphasis

    /**
     * \brief parameter 'line'을 강조한다.
     *
     * \param line 강조할 line
     */
    void emphasis_on_line(const Line &line);

    /**
     * \brief parameter 'screen'을 강조한다.
     *
     * \param screen 강조할 screen
     */
    void emphasis_on_screen(const Screen &screen);

    /**
     * \brief 현재 위치(current position)을/를 파악해 option navigation을 강조한다.
     *
     * \details 자세한 내용은 구현 코드를 참고할 것.
     */
    void emphasis_on_option_navigation();

private:
    /**
     * \brief parameter 'line'을 강조한다.
     *
     * \param line 강조할 line
     */
    void emphasis(const Line &line);

    /**
     * \brief parameter 'screen'에 속한 모든 line의 강조를 해제한다.
     *
     * \param screen 강조를 해제할 screen
     */
    void emphasis_off(const Screen &screen);

    /**
     * \brief parameter 'screen'에 속한 line 중 index가 beggin 이상 end 이하인 line의 강조를 해제한다.
     *
     * \param screen 강조를 해제할 screen
     * \param beggin 강조를 해제할 line의 beggin index
     * \param end 강조를 해제할 line의 end index
     */
    void emphasis_off_by_position(const Screen &screen, uint8_t beggin, uint8_t end);

private:
    uint8_t mCurrentPosition{0};

    uint8_t mMaximumPosition{0};

    // menu navigation

    Line mMenuPrev{1, 3, "<"};

    Line mMenuHome{8, 3, "home"};

    Line mMenuNext{18, 3, ">"};

    Screen mMenuNavigation{mMenuPrev, mMenuHome, mMenuNext};

    // option navigation

    Line mOptionPrev{1, 2, "<"};

    Line mOptionNext{18, 2, ">"};

    Line mOptionExit{1, 3, "exit"};

    Line mOptionSave{16, 3, "save"};

    Screen mOptionNavigation{mOptionPrev, mOptionNext, mOptionExit, mOptionSave};
};
