// MFRC522 의 가짜 정의 — 실제 헤더(lib\MFRC522\MFRC522.h)의 멤버 함수를 카드 모델로 구현.
// 모델 규칙(ISO14443-3 · MIFARE Classic):
//  - REQA 는 IDLE 카드만 응답(→READY). HALT 는 무응답. READY/ACTIVE/AUTH 카드는 예상 밖 명령 → IDLE, 무응답.
//  - 리더가 암호 상태면 평문 명령이 깨진다(카드 → IDLE).
//  - 인증 실패·쓰기 실패 → 카드 IDLE. 리더 쪽 수신 오류(readErr)는 카드 상태를 바꾸지 않는다.
//  - HLTA 는 ACTIVE(평문)·AUTH(암호) 카드에만 듣는다.
#include "harness.h"
#include <MFRC522.h>
#include <string.h>

SimCard *g_card;
SimCardState g_cardState = CARD_OFF;
bool g_readerCrypto;
static int8_t s_authSector = -1;
static bool   s_authKeyB;   // 마지막 인증이 KEY_B 였나 — 트레일러 쓰기 규칙에 쓴다

static const uint8_t kTraceQKey[6] = {0x90, 0x25, 0x84, 0x71, 0x84, 0x72};

static void card_common(SimCard &c, uint8_t uidLast)
{
    memset(&c, 0, sizeof(c));
    c.uid[0] = 0x11; c.uid[1] = 0x22; c.uid[2] = 0x33; c.uid[3] = uidLast;
    c.sak = 0x08;
    c.readErrBlock = -1;
    c.nackBlock = -1;
    for (uint8_t s = 0; s < 16; ++s) memset(c.keyA[s], 0xFF, 6);
}
void card_init_traceq(SimCard &c, uint8_t uidLast)
{
    card_common(c, uidLast);
    for (uint8_t s = 0; s < 16; ++s)
    {
        memcpy(c.keyB[s], kTraceQKey, 6); c.keyBAuth[s] = true;
        c.data[s * 4 + 3][6] = 0x0F; c.data[s * 4 + 3][7] = 0x00; c.data[s * 4 + 3][8] = 0xFF;   // 접근조건 011 바이트(실물과 같게)
    }
}
void card_init_foreign(SimCard &c, uint8_t uidLast)
{
    card_common(c, uidLast);
    for (uint8_t s = 0; s < 16; ++s)
    {
        memset(c.keyB[s], 0xA5, 6); c.keyBAuth[s] = false;   // 공장 운송 접근조건(FF 07 80): KEY_A 로 전부 쓰기 가능
        c.data[s * 4 + 3][6] = 0xFF; c.data[s * 4 + 3][7] = 0x07; c.data[s * 4 + 3][8] = 0x80;
    }
}
void card_place(SimCard *c) { g_card = c; g_cardState = CARD_IDLE; }
void card_remove() { g_card = nullptr; g_cardState = CARD_OFF; }

static bool card_op()   // 인증·읽기·쓰기 1회. false = 카드가 이미 없다
{
    if (g_card == nullptr || g_powerCut) return false;
    ++g_card->opCount;
    if (g_card->removeAfterOps && g_card->opCount > (uint16_t)g_card->removeAfterOps)
    {
        card_remove();
        return false;
    }
    return true;
}
static void card_idle() { if (g_card) g_cardState = CARD_IDLE; s_authSector = -1; }

MFRC522::MFRC522(byte, byte) {}
// 죽은 리더 흉내(0x00/0xFF) → 제품이 Reinitialize() 를 탄다.
uint8_t g_versionReg = 0x92;
// RC522 소프트 리셋은 TxControlReg 를 초기값(안테나 OFF)으로 되돌린다 → 올려 둔 카드가 전원을 잃고
// 정지(HALT)가 풀린다. 실물 데이터시트 근거(4차 B 갈래). 횟수는 g_fieldDrop.
uint16_t g_fieldDrop;
void sim_field_drop()
{
    ++g_fieldDrop;
    if (g_card != nullptr) g_cardState = CARD_IDLE;
    g_readerCrypto = false;
    s_authSector = -1;
}
void MFRC522::PCD_WriteRegister(byte reg, byte val)
{
    if (reg == CommandReg && val == PCD_SoftReset) sim_field_drop();
}
byte MFRC522::PCD_ReadRegister(byte reg)
{
    if (reg == VersionReg) return g_versionReg;
    return 0x00;   // CommandReg: PowerDown 해제 = soft reset 완료
}
void MFRC522::PCD_AntennaOn() {}
void MFRC522::PCD_StopCrypto1() { g_readerCrypto = false; }

MFRC522::StatusCode MFRC522::PICC_RequestA(byte *atqa, byte *size)
{
    if (g_card == nullptr || g_powerCut) return STATUS_TIMEOUT;
    if (g_readerCrypto) { card_idle(); return STATUS_TIMEOUT; }
    if (g_cardState == CARD_IDLE)
    {
        g_cardState = CARD_READY;
        if (atqa && size && *size >= 2) { atqa[0] = 0x04; atqa[1] = 0x00; *size = 2; }
        return STATUS_OK;
    }
    if (g_cardState != CARD_HALT) card_idle();
    return STATUS_TIMEOUT;
}

MFRC522::StatusCode MFRC522::PICC_WakeupA(byte *atqa, byte *size)
{
    // WUPA: IDLE 뿐 아니라 HALT 카드도 응답한다(ISO14443-3 6.3). 그 밖은 REQA 와 같다.
    if (g_card == nullptr || g_powerCut) return STATUS_TIMEOUT;
    if (g_readerCrypto) { card_idle(); return STATUS_TIMEOUT; }
    if (g_cardState == CARD_IDLE || g_cardState == CARD_HALT)
    {
        g_cardState = CARD_READY;
        if (atqa && size && *size >= 2) { atqa[0] = 0x04; atqa[1] = 0x00; *size = 2; }
        return STATUS_OK;
    }
    card_idle();
    return STATUS_TIMEOUT;
}

bool MFRC522::PICC_ReadCardSerial()
{
    if (g_card == nullptr || g_cardState != CARD_READY) return false;
    g_cardState = CARD_ACTIVE;
    uid.size = 4;
    memcpy(uid.uidByte, g_card->uid, 4);
    uid.sak = g_card->sak;
    return true;
}

MFRC522::StatusCode MFRC522::PICC_HaltA()
{
    if (g_card == nullptr) return STATUS_OK;
    if (g_card->loseHalt) { g_card->loseHalt = false; return STATUS_OK; }
    if ((g_cardState == CARD_ACTIVE && !g_readerCrypto) || (g_cardState == CARD_AUTH && g_readerCrypto))
        g_cardState = CARD_HALT;
    else if (g_cardState == CARD_ACTIVE || g_cardState == CARD_AUTH)
        card_idle();
    return STATUS_OK;   // 실물도 무응답(타임아웃)을 성공으로 돌려준다
}

MFRC522::StatusCode MFRC522::PCD_Authenticate(byte cmd, byte block, MIFARE_Key *key, Uid *)
{
    if (!card_op()) { g_readerCrypto = false; return STATUS_TIMEOUT; }
    SimCard &c = *g_card;
    ++c.authCount;
    const uint8_t sector = block / 4;
    bool ok = (g_cardState == CARD_ACTIVE && !g_readerCrypto) ||
              (g_cardState == CARD_AUTH && g_readerCrypto);   // nested 는 암호 상태로만
    if (ok)
    {
        if (cmd == PICC_CMD_MF_AUTH_KEY_B)
            ok = c.keyBAuth[sector] && memcmp(key->keyByte, c.keyB[sector], 6) == 0;
        else
            ok = memcmp(key->keyByte, c.keyA[sector], 6) == 0;
    }
    if (ok && c.failAuthAt && c.authCount == (uint16_t)c.failAuthAt) ok = false;
    if (!ok)
    {
        card_idle();
        g_readerCrypto = false;
        return STATUS_TIMEOUT;
    }
    g_cardState = CARD_AUTH;
    s_authSector = (int8_t)sector;
    s_authKeyB = (cmd == PICC_CMD_MF_AUTH_KEY_B);
    g_readerCrypto = true;
    return STATUS_OK;
}

static bool card_can_rw(byte block)
{
    return g_cardState == CARD_AUTH && g_readerCrypto && s_authSector == (int8_t)(block / 4);
}

MFRC522::StatusCode MFRC522::MIFARE_Read(byte block, byte *buffer, byte *size)
{
    if (buffer == nullptr || *size < 18) return STATUS_NO_ROOM;
    if (!card_op()) return STATUS_TIMEOUT;
    SimCard &c = *g_card;
    ++c.readCount;
    if (!card_can_rw(block)) { card_idle(); return STATUS_TIMEOUT; }
    if (c.readErrBlock == (int16_t)block && c.readErrTimes)
    {
        if (c.readErrSkip) { --c.readErrSkip; }
        else
        {
            --c.readErrTimes;
            return STATUS_CRC_WRONG;   // 리더 쪽 수신 오류 — 카드는 그대로
        }
    }
    memcpy(buffer, c.data[block], 16);
    buffer[16] = buffer[17] = 0;
    *size = 18;
    return STATUS_OK;
}

MFRC522::StatusCode MFRC522::MIFARE_Write(byte block, byte *buffer, byte size)
{
    if (buffer == nullptr || size < 16) return STATUS_INVALID;
    if (!card_op()) return STATUS_TIMEOUT;
    SimCard &c = *g_card;
    ++c.writeCount;
    if (!card_can_rw(block)) { card_idle(); return STATUS_TIMEOUT; }
    if (c.failWriteAt && c.writeCount == (uint16_t)c.failWriteAt) { card_idle(); return STATUS_MIFARE_NACK; }
    // MF1S50 표7: 접근조건 011(TraceQ) 트레일러는 KEY_B 인증에서만 쓸 수 있다 — KEY_A 면 NACK.
    if ((block & 3) == 3 && c.keyBAuth[block / 4] && !s_authKeyB) return STATUS_MIFARE_NACK;
    // 특정 블록만 NACK — 카드 상태는 유지(다음 블록 쓰기는 성공한다).
    if (c.nackBlock == (int16_t)block)
    {
        if (c.nackBlockSkip) { --c.nackBlockSkip; }
        else return STATUS_MIFARE_NACK;
    }
    memcpy(c.data[block], buffer, 16);
    if ((block & 3) == 3)
    {
        const uint8_t s = block / 4;
        memcpy(c.keyA[s], buffer, 6);
        memcpy(c.keyB[s], buffer + 10, 6);
        // 트레일러 C2 비트(byte8 bit3): g3=3(TraceQ) 이면 1 → KEY_B 인증 가능, g3=1(공장) 이면 0 → 불가
        c.keyBAuth[s] = (buffer[8] & 0x08) != 0;
    }
    return STATUS_OK;
}

// 실물과 같은 순수 함수(MFRC522.cpp 1.2.0 그대로)
MFRC522::PICC_Type MFRC522::PICC_GetType(byte sak)
{
    sak &= 0x7F;
    switch (sak)
    {
    case 0x04: return PICC_TYPE_NOT_COMPLETE;
    case 0x09: return PICC_TYPE_MIFARE_MINI;
    case 0x08: return PICC_TYPE_MIFARE_1K;
    case 0x18: return PICC_TYPE_MIFARE_4K;
    case 0x00: return PICC_TYPE_MIFARE_UL;
    case 0x10:
    case 0x11: return PICC_TYPE_MIFARE_PLUS;
    case 0x01: return PICC_TYPE_TNP3XXX;
    case 0x20: return PICC_TYPE_ISO_14443_4;
    case 0x40: return PICC_TYPE_ISO_18092;
    default:   return PICC_TYPE_UNKNOWN;
    }
}
void MFRC522::MIFARE_SetAccessBits(byte *b, byte g0, byte g1, byte g2, byte g3)
{
    byte c1 = ((g3 & 4) << 1) | ((g2 & 4) << 0) | ((g1 & 4) >> 1) | ((g0 & 4) >> 2);
    byte c2 = ((g3 & 2) << 2) | ((g2 & 2) << 1) | ((g1 & 2) << 0) | ((g0 & 2) >> 1);
    byte c3 = ((g3 & 1) << 3) | ((g2 & 1) << 2) | ((g1 & 1) << 1) | ((g0 & 1) << 0);
    b[0] = (~c2 & 0xF) << 4 | (~c1 & 0xF);
    b[1] = c1 << 4 | (~c3 & 0xF);
    b[2] = c3 << 4 | c2;
}
