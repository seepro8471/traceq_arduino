// 사장님 결정(09-28): 게이트웨이가 PC 에서 받은 본체번호(G1)를 **설정값(EEPROM)에도 반영**한다 — 다를 때만 1회.
//  잠그는 것: ① 다르면 바뀐다 ② 같으면 EEPROM 을 안 쓴다 ③ 0 은 안 쓴다(PC 가 막았어도 찢긴 전문이 0 을 준다)
//  ④ 상한 밖은 안 쓴다(세터가 자르면 매 전문마다 달라 EEPROM 을 계속 쓴다) ⑤ 게이트웨이가 아닌 기기는 안 바뀐다
//  ⑥ 전원을 다시 켜 첫 G1 이 오기 전에도 그 번호로 기록한다(= 이 변경의 이유)
#include "common.h"

static SimCard sc;

static int dev_of(const SimCard &c, uint8_t block)   // 레코드 머리 2바이트 = 기기(본체)번호
{
    int d = 0;
    memcpy(&d, c.data[block], 2);
    return d;
}

// G1 값만 다른 온전한 레코드 한 벌(G5 로 끝낸다 — 꼬리가 없으면 통째로 버린다)
static void send_g1(const char *gate)
{
    char f[96];
    snprintf(f, sizeof(f), "G1%s;G22026;9;28;3;10;00;0;G3PT0001;HONG;;G4S;;;G5;", gate);
    serial_inject(f, strlen(f));
    GUARDED(serialEvent());
    run_loops(1);
}

static void as_type(char t)
{
    deviceOption.SetType(t);
    hard_reset(false, 2);
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('G');
    deviceOption.SetNumber(11);

    // ── ① 다르면 설정값이 바뀐다 ──
    {
        g_eepromWrites = 0;
        send_g1("0007");
        tlog("  1 G1=0007 · 설정값=%d · EEPROM 쓴 바이트=%lu\n",
             deviceOption.GetNumber(), (unsigned long)g_eepromWrites);
        CHECK(deviceOption.GetNumber() == 7 && g_eepromWrites > 0,
              "1 PC 번호가 설정값과 다르면 설정값이 그 번호로 바뀐다");
    }

    // ── ② 같은 번호가 또 오면 EEPROM 을 안 쓴다(전문마다 쓰면 수명을 깎는다) ──
    {
        g_eepromWrites = 0;
        send_g1("0007");
        send_g1("0007");
        send_g1("0007");
        tlog("  2 같은 번호 3회 · 설정값=%d · EEPROM 쓴 바이트=%lu\n",
             deviceOption.GetNumber(), (unsigned long)g_eepromWrites);
        // ★이 CHECK 가 잠그는 것은 **"전문마다 EEPROM 에 쓰지 않는다"(수명)** 다 — 사장님 계약의 핵심.
        //  `!=` 관문 자체는 잠그지 않는다: 그것을 지워도 `EEPROM.put` 이 같은 바이트를 안 써서 쓴 바이트는
        //  0 그대로다(변이 N3 로 측정). 읽기 횟수로 잠그면 구현 세부를 잠그는 것이 된다 → 제품에 판정 주석.
        CHECK(deviceOption.GetNumber() == 7 && g_eepromWrites == 0,
              "2 같은 번호가 반복돼도 EEPROM 에 한 바이트도 쓰지 않는다(수명)");
    }

    // ── ③ PC 가 0 을 주면(찢긴 전문) 설정값을 건드리지 않는다 ──
    {
        g_eepromWrites = 0;
        send_g1("0000");
        tlog("  3 G1=0000 · 설정값=%d · 쓴 바이트=%lu · 태그에 쓸 번호=%d\n",
             deviceOption.GetNumber(), (unsigned long)g_eepromWrites,
             gatewayProcessor.effective_number(deviceOption.GetNumber()));
        CHECK(deviceOption.GetNumber() == 7 && g_eepromWrites == 0,
              "3 G1=0000 은 설정값을 바꾸지 않는다(0 = 지정 없음 · 리더는 PC 를 믿지 않는다)");
        CHECK(gatewayProcessor.effective_number(deviceOption.GetNumber()) == 7,
              "3b 0 을 받아도 태그에는 설정값(7)을 쓴다 — effective_number 폴백 그대로");
    }

    // ── ④ 상한(999) 밖이면 안 쓴다 — 쓰면 세터가 잘라 매 전문마다 달라 EEPROM 을 계속 쓴다 ──
    {
        g_eepromWrites = 0;
        send_g1("1234");
        send_g1("1234");
        tlog("  4 G1=1234 2회 · 설정값=%d · 쓴 바이트=%lu\n",
             deviceOption.GetNumber(), (unsigned long)g_eepromWrites);
        CHECK(deviceOption.GetNumber() == 7 && g_eepromWrites == 0,
              "4 상한(999) 밖 번호는 설정값을 바꾸지 않는다 — 관문이 없으면 세터가 잘라 틀린 999 가 저장된다");
        // ★RAM 경로(태그·화면에 쓸 번호)도 같은 상한 — 여기만 받으면 재기동 전후 번호가 갈린다(14차 II-G P3-4)
        CHECK(gatewayProcessor.effective_number(deviceOption.GetNumber()) == 7,
              "4b 상한 밖 번호는 태그·화면에 쓸 번호(RAM)도 바꾸지 않는다 — EEPROM 과 같은 정본 상한");
    }

    // ── ⑤ 게이트웨이가 아닌 기기는 G1 전문으로 설정값이 안 바뀐다 ──
    {
        as_type('W');
        deviceOption.SetNumber(11);
        g_eepromWrites = 0;
        send_g1("0007");
        tlog("  5 세척기에 G1=0007 · 설정값=%d · 쓴 바이트=%lu\n",
             deviceOption.GetNumber(), (unsigned long)g_eepromWrites);
        CHECK(deviceOption.GetNumber() == 11 && g_eepromWrites == 0,
              "5 게이트웨이가 아닌 기기는 G1 전문으로 기기번호가 바뀌지 않는다");
    }

    // ── ⑥ 이 변경의 이유: 전원을 다시 켜 **첫 G1 이 오기 전**에도 그 번호로 기록한다 ──
    //    종전엔 mGateNumber = -1 이라 설치 때 넣어 둔 기기 자체 번호(11)로 찍혔다.
    {
        as_type('G');
        deviceOption.SetNumber(11);
        send_g1("0007");                              // PC 가 한 번 알려 준다 → 설정값 7
        hard_reset(false, 2);                         // 전원 재기동 — mGateNumber 는 -1 로 돌아간다
        tlog("  6a 재기동 직후 설정값=%d · GateNumber=%d\n",
             deviceOption.GetNumber(), gatewayProcessor.GateNumber());
        CHECK(deviceOption.GetNumber() == 7 && gatewayProcessor.GateNumber() < 0,
              "6a 재기동 뒤 PC 번호는 설정값으로 남고 RAM 값(GateNumber)은 미수신(-1)이다");

        make_tag(sc, 0x61, SCOPE_TYPE_TAG, 61, "SC0061", "S0061");
        set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
        logs_clear();
        touch(sc, 2, 4);                              // 첫 G1 전의 폴백 기록
        tlog("  6b 첫 G1 전 기록된 본체번호=%d\n", dev_of(sc, SECTOR1_GATEWAY));
        CHECK(dev_of(sc, SECTOR1_GATEWAY) == 7,
              "6b 첫 G1 이 오기 전에도 PC 번호(7)로 기록된다 — 이 변경의 이유");
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
