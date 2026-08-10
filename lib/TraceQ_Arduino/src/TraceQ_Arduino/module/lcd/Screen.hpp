#pragma once

#include "TraceQ_Arduino/module/lcd/Line.hpp"

/**
 * \class Screen
 * \brief 다수의 Line을 관리할 수 있는 클래스.
 * \details 클래스 생성 이후에도 Line을 추가할 수 있으나, 한 번 등록한 Line을 지울 수는 없다.
 *
 * \since 1.0
 */
class Screen
{

public:
    /// \since 1.2.g
    Screen() = default;

    explicit Screen(Line &line)
    {
        AddFixedLine(line);
    }

    Screen(Line &l1, Line &l2) : Screen(l1)
    {
        AddFixedLine(l2);
    }

    Screen(Line &l1, Line &l2, Line &l3) : Screen(l1, l2)
    {
        AddFixedLine(l3);
    }

    Screen(Line &l1, Line &l2, Line &l3, Line &l4) : Screen(l1, l2, l3)
    {
        AddFixedLine(l4);
    }

    inline Line &operator[](uint8_t index) const
    {
        return *mLines[index];
    }

    void AddFixedLine(Line &line);

    /**
     * \brief 클래스에 line을/를 추가한다.
     *
     * \details 별도의 중복 검사는 거치지 않으며, 클래스가 관리중인 line의 개수만을 검사한다. 만약 그 수가
     * 'MAX_LINES'을/를 넘어섰다면 더 이상 추가할 수 없다.
     *
     * \param line 추가할 line
     */
    void AddLine(Line &line);

    inline uint8_t GetLineCount() const
    {
        return mLineCount;
    }

protected:
    static void adjust_line_cursor(uint8_t index, Line &line);

private:
    Line *mLines[MAX_LINES]{};

    /**
     * \brief 현재 아이템에 등록된 Line의 수.
     */
    uint8_t mLineCount{0};
};
