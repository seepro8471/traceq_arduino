// 하네스 자기 점검 — 이 시험은 **빨강이어야** 한다(회귀 종 수에 안 든다 · runall.sh 가 끝에 한 번 돌린다).
//  done() 안의 관문 넷(전원 차단이 켜진 채 끝남 · 가득 찬 로그에 물음 · 입력 잘림 · loop 끊김)이 요약(g_fail · 실패 목록)에 닿는지 본다.
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
    static char big[300];
    memset(big, 'l', 257); big[257] = 0;
    buttons_script(big);                                                       // 257자 — 가짜 칸(256)을 넘어 잘린다(입력 잘림 관문 · 19차)
    buttons_script("s");                                                       // 홈은 SELECT 만 읽는다 — 그것을 한 번 '안 눌림' 으로 소진
    g_btnIdleLimit = 10;
    run_loops(20);                                                             // 스크립트를 다 쓴 뒤 버튼 대기 한도 → loop 끊김 관문(19차)
    g_btnIdleLimit = 30000UL;
    buttons_script("");
    g_powerCut = true;                                                         // 켠 채로 끝낸다
    done();
    for (;;) {}
}
