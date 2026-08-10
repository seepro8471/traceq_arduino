#include "Line.hpp"

uint8_t Line::GetCol() const
{
    return mCol;
}

uint8_t Line::GetRow() const
{
    return mRow;
}

void Line::SetCol(const uint8_t col)
{
    mCol = col;
    if (col >= 1)
    {
        mEmphasisCol = col - 1;
    }
}

void Line::SetRow(const uint8_t row)
{
    mRow = row;
    if (row >= 1)
    {
        mEmphasisRow = row - 1;
    }
}

void Line::SetCursor(const uint8_t col, const uint8_t row)
{
    SetCol(col);
    SetRow(row);
}

char *Line::GetContent()
{
    return const_cast<char *>(mContent);
}

const char *Line::GetContent() const
{
    return mContent;
}

uint8_t Line::GetUniqueVar() const
{
    return mUniqueVar;
}

uint8_t Line::GetEmphasisRow() const
{
    return mEmphasisRow;
}

uint8_t Line::GetEmphasisCol() const
{
    return mEmphasisCol;
}

void Line::SetContent(const char *content)
{
    this->mContent = content;
}
