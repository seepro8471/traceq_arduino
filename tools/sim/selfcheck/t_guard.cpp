// 하네스 자기 점검 — 이 시험은 **빨강이어야** 한다(회귀 종 수에 안 든다 · runall.sh 가 끝에 한 번 돌린다).
//  done() 안의 관문 둘(전원 차단이 켜진 채 끝남 · 가득 찬 로그에 물음)이 요약(g_fail · 실패 목록)에 닿는지 본다.
//  18차: gdb 중단점이 done() 첫머리라 "전원 차단" 관문이 여러 회차 동안 한 번도 결과에 닿지 않았다(음성대조 탐침으로 발견).
#include "common.h"

static SimCard mgr;

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
    logs_clear();
    for (uint8_t i = 0; i < 60; ++i) touch(mgr);                               // LCD 로그(1536B)를 채운다
    CHECK(!lcd_has("NOPE-NOT-THERE"), "self: absence asked on a full LCD log");   // 판정은 통과하지만 포화 표지가 선다
    g_powerCut = true;                                                         // 켠 채로 끝낸다
    done();
    for (;;) {}
}
