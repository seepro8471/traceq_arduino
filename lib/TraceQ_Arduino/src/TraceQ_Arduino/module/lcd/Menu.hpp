#pragma once

#include "TraceQ_Arduino/module/lcd/Screen.hpp"

/**
 * \class Menu
 * \brief 다수의 Screen을 관리할 수 있는 클래스. 현재 화면 구성의 가장 큰 단위이다.
 * \details 클래스 생성 이후에도 Screen을 추가할 수 있으나, 한 번 등록한 Screen을 지울 수는 없다.
 *
 * \since 1.0
 */
class Menu
{

public:
    Menu() = default;

    explicit Menu(Screen &screen)
    {
        AddScreen(screen);
    }

    /**
     * \brief 클래스에 screen을/를 추가한다.
     *
     * \details 별도의 중복 검사는 거치지 않으며, 클래스가 관리중인 screen의 개수만을 검사한다. 만약 그 수가
     * 'MAX_SCREENS'을/를 넘어섰다면 더 이상 추가할 수 없다.
     *
     * \param screen 추가할 screen
     */
    void AddScreen(Screen &screen);

    uint8_t GetTotalScreen() const;

    uint8_t GetCurrentScreen() const;

    Screen GetScreen(uint8_t index);

private:
    Screen *mScreens[MAX_SCREENS]{};

    uint8_t mTotalScreen{};

    uint8_t mCurrentScreen{};
};
