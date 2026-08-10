#pragma once

#include "TraceQ_Arduino/module/lcd/Configuration.hpp"

/**
 * \class Line
 * \brief 화면(lcd)에 표시/관리될 최소한의 단위. 지정된 cursor col, row에 문자열을 출력할 수 있도록 한다.
 *
 * \details Line이라고 이름지어지긴 했지만 하나의 row에 여러 개의 Line Content가 출력될 수 있다.
 *
 * \details 1.2.g 버전에서 unique, content만을 인자로 받는 생성자를 추가하였고, col/row의 값을 수정할 수 있도록 하였다.
 *
 * \since 1.0
 */
class Line
{

public:
    Line(const uint8_t unique, const char *content) : mUniqueVar(unique),
                                                      mContent(content){};

    Line(const uint8_t col, const uint8_t row, const char *content) : mCol(col),
                                                                      mRow(row),
                                                                      mContent(content)
    {
        if (col >= 1)
        {
            mEmphasisCol = col - 1;
        }
        if (row >= 1)
        {
            mEmphasisRow = row - 1;
        }
    }

    Line(const uint8_t col, const uint8_t row, const uint8_t unique, const char *content) : Line(col, row, content)
    {
        this->mUniqueVar = unique;
    }

    inline bool operator==(const Line &line) const
    {
        if (this->mUniqueVar == line.GetUniqueVar())
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    uint8_t GetCol() const;

    uint8_t GetRow() const;

    /**
     * \param col 변경할 column
     *
     * \since 1.2.g
     */
    void SetCol(uint8_t col);

    /**
     * \param row 변경할 row
     *
     * \since 1.2.g
     */
    void SetRow(uint8_t row);

    /**
     * \param col 변경할 column
     * \param row 변경할 row
     *
     * \since 1.2.g
     */
    void SetCursor(uint8_t col, uint8_t row);

    /// \note c++11에선 string literal -> char *로의 변환을 지원하지 않는다.
    char *GetContent();

    const char *GetContent() const;

    uint8_t GetUniqueVar() const;

    uint8_t GetEmphasisRow() const;

    uint8_t GetEmphasisCol() const;

    void SetContent(const char *content);

protected:
private:
    /**
     * \brief Content가 표시될 cursor col.
     */
    uint8_t mCol{0};

    /**
     * \brief Content가 표시될 cursor row.
     */
    uint8_t mRow{0};

    /**
     * \brief Content가 강조 될 때의 cursor col.
     */
    uint8_t mEmphasisCol{0};

    /**
     * \brief Content가 강조 될 때의 cursor row.
     */
    uint8_t mEmphasisRow{0};

    /**
     * \brief Line instance을/를 구분하기 위해 갖는 고유한 값.
     */
    uint8_t mUniqueVar{};

    /**
     * \brief 화면(lcd)에 표시되는 등 다양한 용도로 사용될 문자열.
     */
    const char *mContent;
};
