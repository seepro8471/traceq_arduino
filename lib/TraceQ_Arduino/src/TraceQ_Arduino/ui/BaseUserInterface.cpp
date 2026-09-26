#include "BaseUserInterface.hpp"

BaseUserInterface::Position BaseUserInterface::compare_position() const
{
    if (mCurrentPosition == mMaximumPosition)
    {
        return Position::Equal;
    }
    if (mCurrentPosition == (mMaximumPosition - 1))
    {
        return Position::Lower1;
    }
    if (mCurrentPosition == (mMaximumPosition - 2))
    {
        return Position::Lower2;
    }
    return BaseUserInterface::Position::LowerN;
}

void BaseUserInterface::adjust_position(bool decrement)
{
    if (decrement)
    {
        if (mCurrentPosition == 0)
        {
            mCurrentPosition = mMaximumPosition;
        }
        else
        {
            mCurrentPosition--;
        }
    }
    else
    {
        if (mCurrentPosition == mMaximumPosition)
        {
            mCurrentPosition = 0;
        }
        else
        {
            mCurrentPosition++;
        }
    }
}

void BaseUserInterface::print_title(const char *title)
{
    Info_cstr(0, 0, title);
}

void BaseUserInterface::print_title(const __FlashStringHelper *title)
{
    Info(0, 0, title);
}

void BaseUserInterface::print_option_nagivation()
{
    // clear — 홈 캐시 무효화는 호출자(UserInterface::select)가 한다
    mLcd.clear();
    // print
    print_screen(mOptionNavigation);
    // set position
    setCurrentPosition(0);
    setMaximumPosition(3);
    // emphasis
    emphasis_on_option_navigation();
}

void BaseUserInterface::print_option_navigation(uint8_t beggin, uint8_t end)
{
    print_sceen_by_position(mOptionNavigation, beggin, end);
}

void BaseUserInterface::print_menu_navigation()
{
    print_screen(mMenuNavigation);
}

void BaseUserInterface::emphasis_on_line(const Line &line)
{
    // clear option navigation emphasis
    emphasis_off_by_position(mOptionNavigation, 2, 3);
    clear_line(line.GetEmphasisRow());
    // compare and emphasis
    switch (compare_position())
    {
    case Position::Equal:
    {
        emphasis(mOptionNavigation[3]);
        break;
    }
    case Position::Lower1:
    {
        emphasis(mOptionNavigation[2]);
        break;
    }
    case Position::Lower2:
    case Position::LowerN:
    {
        mLcd.setCursor(mCurrentPosition, line.GetEmphasisRow());
        mLcd.print(EMPHASIS_ON);
        break;
    }
    default:
        // bug
        util_soft_reset();
    }
}

void BaseUserInterface::emphasis_on_screen(const Screen &screen)
{
    // clear emphasis
    emphasis_off(screen);
    emphasis_off(mMenuNavigation);
    // compare and emphasis
    switch (compare_position())
    {
    case Position::Equal:
    {
        emphasis(mMenuNavigation[2]);
        break;
    }
    case Position::Lower1:
    {
        emphasis(mMenuNavigation[1]);
        break;
    }
    case Position::Lower2:
    {
        emphasis(mMenuNavigation[0]);
        break;
    }
    case Position::LowerN:
    {
        emphasis(screen[getCurrentPosition()]);
        break;
    }
    default:
        // bug
        util_soft_reset();
    }
}

void BaseUserInterface::emphasis_on_option_navigation()
{
    // clear emphasis
    emphasis_off(mOptionNavigation);
    // compare and emphasis
    switch (compare_position())
    {
    case Position::Equal:
    {
        emphasis(mOptionNavigation[3]); // save
        break;
    }
    case Position::Lower1:
    {
        emphasis(mOptionNavigation[2]); // exit
        break;
    }
    case Position::Lower2:
    {
        emphasis(mOptionNavigation[1]); // next
        break;
    }
    case Position::LowerN:
    {
        emphasis(mOptionNavigation[0]); // prev
        break;
    }
    default:
    {
        // error
        util_soft_reset();
    }
    }
}

void BaseUserInterface::emphasis(const Line &line)
{
    mLcd.setCursor(line.GetEmphasisCol(), line.GetRow());
    mLcd.print(EMPHASIS_ON);
}

void BaseUserInterface::emphasis_off(const Screen &screen)
{
    if (screen.GetLineCount() == 0) return;   // 0 줄 방어(LcdPrinter::print_screen 과 같이)
    emphasis_off_by_position(screen, 0, screen.GetLineCount() - 1);
}

void BaseUserInterface::emphasis_off_by_position(const Screen &screen, uint8_t beggin, uint8_t end)
{
    // beggin이 end보다 큰 경우 치환
    if (beggin > end)
    {
        const uint8_t temp = beggin;
        beggin = end;
        end = temp;
    }
    const uint8_t lineCount = screen.GetLineCount() - 1;
    // end가 screen에 속한 line의 수보다 큰 경우 조정
    if (end > lineCount)
    {
        end = lineCount;
    }
    for (uint8_t i = beggin; i <= end; ++i)
    {
        // get line by index
        const auto &line{screen[i]};
        // set cursor and print 'emphasis off'
        mLcd.setCursor(line.GetEmphasisCol(), line.GetRow());
        mLcd.print(EMPHASIS_OFF);
    }
}
