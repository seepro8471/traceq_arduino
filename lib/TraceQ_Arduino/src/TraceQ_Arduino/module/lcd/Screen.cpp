#include "Screen.hpp"

void Screen::AddFixedLine(Line &line)
{
    // 1.0의 `<=`는 mLines[MAX_LINES] OOB 쓰기를 허용했음 — `<`로 정정.
    if (mLineCount < MAX_LINES)
    {
        mLines[mLineCount++] = &line;
    }
}

void Screen::AddLine(Line &line)
{
    if (mLineCount < MAX_LINES)
    {
        adjust_line_cursor(mLineCount, line);
        // add
        mLines[mLineCount++] = &line;
    }
}

void Screen::adjust_line_cursor(const uint8_t index, Line &line)
{
    switch (index)
    {
    case 0:
    {
        line.SetCursor(1, 0);
        break;
    }
    case 1:
    {
        line.SetCursor(10, 0);
        break;
    }
    case 2:
    {
        line.SetCursor(1, 1);
        break;
    }
    default:
    { // 3
        line.SetCursor(10, 1);
        break;
    }
    }
}
