#include "Menu.hpp"

void Menu::AddScreen(Screen &screen)
{
    // 1.0의 `<=`는 mScreens[MAX_SCREENS] OOB 쓰기를 허용했음 — `<`로 정정.
    if (mTotalScreen < MAX_SCREENS)
    {
        mScreens[mTotalScreen++] = &screen;
    }
}

uint8_t Menu::GetTotalScreen() const
{
    return mTotalScreen;
}

uint8_t Menu::GetCurrentScreen() const
{
    return mCurrentScreen;
}

Screen Menu::GetScreen(const uint8_t index)
{
    // update current screen index
    mCurrentScreen = index;
    return *mScreens[index];
}
