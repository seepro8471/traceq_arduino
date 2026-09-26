#include "BaseProcessor.hpp"

#include <string.h>

void BaseProcessor::complete_delay() { delay(1); }

bool BaseProcessor::print_tag_number(LcdPrinter &printer)
{
    if (mScanner.Read(SECTOR0_TAG, &mCachedTag, sizeof(Tag)) != RfidResult::Ok)
        return false;
    char buf[6]{};
    snprintf(buf, sizeof(buf), "%05d", mCachedTag.Number);
    printer.Info_cstr(0, 3, buf);
    return true;
}

bool BaseProcessor::read_tag()
{
    return mScanner.Read(SECTOR0_TAG, &mCachedTag, sizeof(Tag)) == RfidResult::Ok;
}

bool BaseProcessor::read_tag_serial()
{
    return mScanner.Read(SECTOR1_TAG_SERIAL, &mCachedTagSerial, sizeof(TagSerial)) == RfidResult::Ok;
}

bool BaseProcessor::read_process()
{
    // 1.0과 동일하게 9바이트만 읽음 (Process는 패딩 포함 >9이나 의미있는 데이터는 9).
    return mScanner.Read(SECTOR1_PROCESS, &mCachedProcess, 9) == RfidResult::Ok;
}

bool BaseProcessor::write_process()
{
    if (mScanner.Write(SECTOR1_PROCESS, &mCachedProcess, 9) == RfidResult::Ok) return true;
    // ★쓰기는 됐는데 확인 읽기만 실패한 경우 — 태그를 한 번 더 읽어 **실제로 커밋됐으면 성공**으로 본다.
    //  종전엔 커밋된 태그에 'Write Error' 를 내고 횟수도 안 올려, 재접촉이 종료로 처리됐다(5차 C P2-2).
    Process onTag{};
    if (mScanner.Read(SECTOR1_PROCESS, &onTag, 9) != RfidResult::Ok) return false;
    return memcmp(&onTag, &mCachedProcess, 9) == 0;
}
