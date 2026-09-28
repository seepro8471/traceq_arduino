#include "GatewayProcessor.hpp"

#include "TraceQ_Arduino/avr/AvrString.hpp"

bool GatewayProcessor::IsGatewayFrame(const char *buffer)
{
    if (buffer == nullptr) return false;
    constexpr size_t kNone = static_cast<size_t>(-1);
    size_t g = kNone;
    for (char d = '1'; d <= '5'; ++d)   // 정적 배열을 두면 시뮬 RAM 관문에 걸린다 — 스택에서 만든다
    {
        const char m[3] = {'G', d, 0};
        const size_t at = find_marker(buffer, m, 0);
        if (at != kNone && (g == kNone || at < g)) g = at;
    }
    if (g == kNone) return false;
    const size_t brace = str_index_of(buffer, '{');
    return brace == kNone || g < brace;
}

bool GatewayProcessor::HasMarker(const char *buffer, const char *marker)
{
    return buffer != nullptr && find_marker(buffer, marker, 0) != static_cast<size_t>(-1);
}

bool GatewayProcessor::NeedsMoreBytes(const char *buffer)
{
    if (buffer == nullptr) return false;
    constexpr size_t kNone = static_cast<size_t>(-1);
    size_t head = kNone;
    for (size_t at = find_marker(buffer, "G1", 0); at != kNone; at = find_marker(buffer, "G1", at + 1))
        head = at;
    if (head == kNone)
        for (size_t at = find_marker(buffer, "G2", 0); at != kNone; at = find_marker(buffer, "G2", at + 1))
            head = at;
    if (head == kNone) return false;                       // 머리가 없으면 기다릴 것도 없다
    return find_marker(buffer + head, "G5", 0) == kNone;   // 머리 **뒤에** 꼬리가 없으면 덜 온 것
}

// [12차 판정 · 재론 금지] 환자 한 벌(키·이름·검사항목 3·일시 등)은 패킷마다 비우지만 **mGateNumber 는
//  남긴다** — 값이 없는 패킷(`G1;`)이면 앞서 받은 본체번호를 쓰는 것이 `effective_number` 의 의도된 폴백이고,
//  델파이는 본체번호를 설정 칸에서 옮겨 오므로 환자마다 바뀌지 않는다(MainFormSo.pas 32,551줄 · 15188·15207줄
//  확인) · 세척관리는 그 값을 아예 안 보낸다. 비워도 `effective_number` 가 설정값(마지막 PC 번호)으로 폴백해
//  결과는 같지만(메뉴로 설정값을 바꾼 직후만 다르다), PC 가 준 값을 버릴 이유가 없다 → 고치지 말 것(GG2 P3-2).
void GatewayProcessor::GatewaySerialEvent(const char *buffer, DefaultRtc &rtc, DeviceOption &deviceOption)
{
    if (buffer == nullptr) return;

    // ★환자정보 한 벌(키·이름·검사항목·검사일시)은 **같은 패킷에서 온 것만** 쓴다.
    //  2.2.5 는 검사일시만 비웠는데, 그래서 G3 구간이 없는 패킷 하나가 오면 형제(키·이름·검사항목)는
    //  직전 검사 것이 남아 "남의 환자 + 검사일시 0" 이 성공음과 함께 다음 스코프에 기록됐다.
    mHasPatientInformation = false;
    mDateTime = LocalDateTime{};
    memset(mPatientKey, 0, sizeof(mPatientKey));
    memset(mPatientName, 0, sizeof(mPatientName));
    memset(mExaminationSubject, 0, sizeof(mExaminationSubject));
    memset(mExaminationSubject2, 0, sizeof(mExaminationSubject2));
    memset(mExaminationSubject3, 0, sizeof(mExaminationSubject3));

    char string[64]{};

    // ★여러 레코드가 한 버퍼에 오면(수신 대기 1초 · 부팅 선택 화면 10초) **마지막 레코드**만 본다.
    //  마지막 `G1`(가장 최근 레코드 시작)부터, G1 이 없는 버퍼는 마지막 `G2` 부터. 종전엔 G1 이 없으면
    //  버퍼 처음부터 봐서 **첫(옛) 환자**가 다음 스코프에 기록됐다(Z2). 모든 필드가 같은 레코드에서 온다.
    constexpr size_t kNone = static_cast<size_t>(-1);
    size_t head = kNone;
    for (size_t at = find_marker(buffer, "G1", 0); at != kNone; at = find_marker(buffer, "G1", at + 1))
        head = at;
    if (head == kNone)
        for (size_t at = find_marker(buffer, "G2", 0); at != kNone; at = find_marker(buffer, "G2", at + 1))
            head = at;
    // 머리(G1·G2)를 잃은 조각은 쓰지 않는다 — RX 링 511바이트 절단의 뒷동이 `G3` 에서 시작하면
    //  환자는 맞고 검사일시만 0 으로 기록되고 성공음이 났다(Z2 P3-1).
    if (head == kNone) return;
    const char *rec = buffer + head;
    // G5 마커로 끝나지 않으면 잘린 레코드 — 받아들이면 검사항목이 통째로 빈칸으로 기록되고 성공음이 났다(Z2 P2).
    //  송신 PC 모두 레코드를 G5 로 끝낸다(올눈 ALLNuN · 델파이 TraceQ MainFormSo.pas:15228 · SeePro · 세척관리).
    if (find_marker(rec, "G5", 0) == kNone) return;

    // G1 = 본체번호 (2.2.6, 사용자 확정). `G1{gate};G2…` 형식.
    // 0(=`0000`)이면 "지정 없음"으로 보고 기기 자체 번호를 쓴다(effective_number).
    // 1.0 은 이 구간을 아예 읽지 않고 항상 기기 자체 번호를 썼다.
    if (find_string(rec, string, sizeof(string), "G1", "G2"))
    {
        const size_t sep = str_index_of(string, ';');
        if (sep != static_cast<size_t>(-1))
        {
            char gateText[8]{};
            str_substring_safe(string, gateText, sizeof(gateText), 0, sep);
            const int parsed = str_atoi(gateText);   // "0002" → 2, 비숫자면 -1
            // 상한은 EEPROM 관문(아래)과 **같은 정본** — RAM 만 받으면 1000 이상이 태그·화면엔 쓰이고 설정값엔
            //  안 들어가 재기동 전후 번호가 갈린다(14차 II-G P3-4). 상한 밖은 앞서 받은 값(없으면 설정값)을 쓴다.
            if (parsed >= 0 && parsed <= DeviceOption::kNumberMax) mGateNumber = static_cast<int16_t>(parsed);
            // ★사장님 결정(09-28): PC 번호를 **설정값에도 반영**한다 — 다를 때만 1회. 그래야 전원을 다시 켜
            //  첫 G1 이 오기 전에도(mGateNumber = -1) 맞는 번호로 기록하고, 설정기·메뉴에도 그 번호가 보인다.
            //  0 과 범위 밖은 쓰지 않는다: PC 는 0 을 막았지만 리더는 그것을 믿지 않고(찢긴 전문이 0 을 준다),
            //  상한을 넘는 값은 세터가 999 로 잘라 **틀린 번호 999 가 저장된다**(14차 측정 정정) → 상한은 정본 하나.
            // [13차 판정 · 재론 금지] `!=` 관문은 **최적화**다(EEPROM 읽기 2회 절약) — 지워도 행위는 같다.
            //  `EEPROM.put` 이 같은 바이트를 안 쓴다는 것을 변이로 측정했다(N3: 전문마다 SetNumber 를 불러도
            //  쓴 바이트 0). 사장님 계약의 핵심인 "전문마다 쓰지 않는다(수명)" 는 t_gnum ② 가 잠근다.
            if (parsed > 0 && parsed <= DeviceOption::kNumberMax && parsed != deviceOption.GetNumber())
                deviceOption.SetNumber(parsed);
        }
    }
    memset(string, 0, sizeof(string));
    if (!find_string(rec, string, sizeof(string), "G3", "G4")) return;

    mHasPatientInformation = substring_for_patient(string);
    if (!mHasPatientInformation) return;

    memset(string, 0, sizeof(string));
    if (find_string(rec, string, sizeof(string), "G4", "G5"))
        substring_for_examination_subject(string);

    memset(string, 0, sizeof(string));
    if (find_string(rec, string, sizeof(string), "G2", "G3"))
        substring_for_local_date_time(string);

    // ★[15차 사장님 A5] G2 가 없거나 무효(2월 30일 등)면 검사일시를 **게이트웨이 시계**로 채운다(폴백과 같은 근거 — 전문이 곧
    //  검사 시작). 종전엔 검사일시 0 인 환자 기록이 성공음으로 남아 세척 표지 판정이 못 봤고, 무효 날짜가 RTC 를 3월 2일로
    //  맞췄다(15차 III-D P3-2·R1). RTC 는 유효한 G2 로만 맞춘다.
    if (mDateTime.Date.Year != 0 && !DefaultRtc::ToDateTime(mDateTime).isValid()) mDateTime = LocalDateTime{};
    if (mDateTime.Date.Year == 0)
    {
        mDateTime = rtc.GetCurrentLocalDateTime();
        return;
    }

    const auto respDateTime = DefaultRtc::ToDateTime(mDateTime);
    if (respDateTime > rtc.GetCurrentDateTime())
        rtc.SetDateTime(DefaultRtc::AddTimeSpan(respDateTime, 0, 2));
}

// [사장님 09-23 판정 · 재론 금지] 받은 환자정보는 기록 뒤에도 비우지 않는다 — 다음 전문(또는 재부팅)까지 산다(한 검사에
//  스코프 둘 · 잠금 t_gg2g G).
void GatewayProcessor::GatewayProcess(int deviceNumber, LcdPrinter &printer)
{
    if (!is_valid(printer)) return;

    // 기록 실패를 조용히 넘기면 환자정보가 들어간 줄 안다 — 알리고 다시 대게(Sm! 도 안 보낸다).
    if (!write_patient_info(deviceNumber))
    {
        printer.CustomWarning(0, 2, 100, 4, F("Write Error"));
        return;
    }

    Serial.println(F("S;"));
    print_to_allnun(3, SECTOR0_TAG, reinterpret_cast<unsigned char *>(&mCachedTag));
    print_to_allnun(7, SECTOR1_TAG_SERIAL, mCachedTagSerial.Serial);
    Serial.println(F("Sm!"));

    complete_delay();
    util_buzzer();

    char buffer[14]{};   // "Scope : 32767" 13자 + NUL
    snprintf(buffer, sizeof(buffer), "Scope : %02d", mCachedTag.Number);
    printer.InfoForWhile_cstr(0, 2, 500, buffer);
}

bool GatewayProcessor::write_no_patient_info(const Gateway &gateway, const char *stringDateTime)
{
    // [5차 판정 · 재론 금지] Status=0 을 **먼저** 쓴다 — 마지막에 두면 실패 시 Status=1(직전 환자) 이
    //  남아 세척기가 남의 환자로 통과시킨다(더 나쁨).
    // ★9차 정정: 뒤 소거가 실패해 옛 환자가 남는 창의 안전망은 세척기 경고가 아니라(사람에게만 알린다)
    //  **다음 세척 시작의 선행 소거**다 — PC 는 Status 를 안 보고 블록 8·9 를 저장한다.
    // ★코드가 주석과 반대였다 — 블록5 를 먼저 쓰고 Status 를 뒤에 써서, 그 사이에 끊기면 "옛 환자 + Status 1 +
    //  새 검사일시" 가 남았다(형제 write_patient_info 는 Status 먼저 · 14차 II-D P3-3). 판정대로 Status 먼저.
    if (!write_process()) return false;
    if (mScanner.Write(SECTOR1_GATEWAY, &gateway, 10) != RfidResult::Ok) return false;

    // SECTOR2 환자 키/이름 두 블록 일괄 0으로 — 인증 1회.
    unsigned char zero[2 * MIFARE_BLOCK_SIZE]{};
    if (mScanner.WriteBlocks(SECTOR2_PATIENT_KEY, 2, zero) != RfidResult::Ok) return false;

    if (mScanner.ClearSector(15) != RfidResult::Ok) return false;
    return mScanner.Write(SECTOR15_EXAMINATION_SUBJECT, stringDateTime, 16) == RfidResult::Ok;
}

bool GatewayProcessor::write_patient_info(int deviceNumber)
{
    // ★이미 '환자정보 있음'(Status=1) 인 태그면 먼저 내린다 — 아래 쓰기가 중간에 실패하면 "Status=1 +
    //  새 키 + 옛 이름" 같은 혼합 태그가 남았다(5차 D). 내려 두면 실패 시 세척기가 '환자정보 없음' 을 알린다.
    //  ★1 만 보면 델파이 시절 Status 2·3(세척기는 3 을 '환자정보 있음' 으로 읽는다)이 선행 정리를 건너뛴다 — 0 이
    //   아니면 전부 내린다(형제 세척기의 진리표와 같은 뜻 · 14차 II-D P3-6 · 도달은 1.0 태그뿐).
    if (mCachedProcess.Status != 0)
    {
        mCachedProcess.Status = 0;
        if (!write_process()) return false;
        unsigned char zero[2 * MIFARE_BLOCK_SIZE]{};   // 옛 환자 블록도 비운다 — PC 는 Status 와 무관하게 블록 8 을 등록번호로 쓴다
        // (이 소거가 실패해 옛 환자가 남으면 다음 세척 시작이 비운다 — washing_start 의 선행 소거)
        if (mScanner.WriteBlocks(SECTOR2_PATIENT_KEY, 2, zero) != RfidResult::Ok) return false;
    }
    // 본체번호는 PC 가 G1 로 준 값 (기기 설정은 미수신 시 폴백일 뿐).
    Gateway gateway{effective_number(deviceNumber), mDateTime};
    if (mScanner.Write(SECTOR1_GATEWAY, &gateway, 10) != RfidResult::Ok) return false;
    if (mScanner.Write(SECTOR2_PATIENT_KEY,  mPatientKey,  16) != RfidResult::Ok) return false;
    if (mScanner.Write(SECTOR2_PATIENT_NAME, mPatientName, 16) != RfidResult::Ok) return false;

    // 1.0과 동일하게 섹터15를 먼저 소거 — 아래 일괄 쓰기가 중간에 실패해도
    // 이전 검사항목이 태그에 잔존하지 않도록 한다.
    if (mScanner.ClearSector(15) != RfidResult::Ok) return false;

    // SECTOR15 데이터 블록은 60,61,62 (트레일러 63). 3블록을 한 번에 일괄 쓰기 — 인증 1회.
    unsigned char buf[3 * MIFARE_BLOCK_SIZE]{};
    memcpy(buf,                          mExaminationSubject,  16);
    memcpy(buf + MIFARE_BLOCK_SIZE,      mExaminationSubject2, 16);
    memcpy(buf + 2 * MIFARE_BLOCK_SIZE,  mExaminationSubject3, 16);
    if (mScanner.WriteBlocks(SECTOR15_EXAMINATION_SUBJECT, 3, buf) != RfidResult::Ok) return false;

    // Status=1(환자정보 기록됨)은 **커밋 플래그** — 환자·검사항목 기록이 모두
    // 성공한 뒤에 마지막으로 세운다. 앞에 두면 중간 실패 시 "환자정보 있음"
    // 인데 실제 블록은 비어/이전 환자인 태그가 남아 세척기의 미기재 경고까지
    // 무력화된다 (1.0 승계 결함 — 2.2.5 수정). 최종 바이트는 동일.
    mCachedProcess.Status = 1;
    return write_process();
}

void GatewayProcessor::GatewayProcessFallback(int deviceNumber, DefaultRtc &rtc, LcdPrinter &printer)
{
    if (!is_valid(printer)) return;
    mCachedProcess.Status = 0;

    const auto dateTime = rtc.GetCurrentDateTime();
    Gateway gateway{effective_number(deviceNumber), DefaultRtc::ToLocalDateTime(dateTime)};
    char format[16]{"YYYYMMDD:hhmmss"};
    const auto stringDateTime = dateTime.toString(format);

    // 기록 실패는 성공으로 알리지 않는다 — 알리고 다시 대게.
    if (!write_no_patient_info(gateway, stringDateTime))
    {
        printer.CustomWarning(0, 2, 100, 4, F("Write Error"));
        return;
    }
    Serial.println(F("Not Patient Info"));   // 기록이 된 뒤에 — 종전엔 기록 전에 보내 실패해도 PC 가 음성을 냈다

    complete_delay();
    // 환자정보 없이 기록했다 — 기록은 됐으므로 실패음(짧게 4회)과 달라야 한다. 길게 2회로 구분 (사장님 09-23).
    // ★소리는 같고 **글자만 더한다** — 형제(세척·소독기)는 같은 문구를 1.6초 띄우는데 여기만 없었다(13차 HH2 P3-7).
    printer.CustomWarning(0, 2, 400, 2, F("No Patient Info"));
}

bool GatewayProcessor::is_valid(LcdPrinter &printer)
{
    // 읽기 실패를 조용히 넘기면 환자정보가 들어간 줄 안다 — 세척·소독기와 같은 알림(RecordProcessor::is_valid).
    if (!print_tag_number(printer) || !read_tag_serial() || !read_process())
    {
        printer.CustomWarning(0, 2, 100, 4, F("Read Error"));
        return false;
    }
    if (mCachedProcess.WashingStatus != 0 || mCachedProcess.DisinfectionStatus != 0)
    {
        // 세척·소독 과정에 들어간 스코프(어느 한쪽 표시라도 선 것)는 새 환자를 붙이지 않는다 — 종전엔 둘 다 선
        // 경우만 막아 "세척만 하고 소독 안 한" 스코프가 통과했다(사장님 09-26). 문자열은 기존 "No Complete" 그대로
        // (SeePro·올눈이 이 문자열로 음성·화면 알림을 낸다 — 바꾸면 안 된다).
        printer.RejectDebug(0, 2, F("No Complete"));
        return false;
    }
    return true;
}

// ★마커(G1~G5)는 **필드 경계**에서만 인정한다 — 맨 앞, ';' 바로 뒤, 값 없는 G1 바로 뒤(세척관리 `G1G2…`).
//  값 안의 같은 글자(등록키 "G1234", 이름 "KIG4M", 검사명 속 "G5")를 마커로 집어 환자정보가 통째로
//  유실되거나 검사명이 오염되던 것(5차 D). 세 PC 의 실제 형식: 델파이·SeePro 는 전부 ';' 뒤, 세척관리만 G1G2 인접.
size_t GatewayProcessor::find_marker(const char *src, const char *marker, size_t begin)
{
    for (size_t at = str_index_of_cstr_range(src, marker, begin); at != static_cast<size_t>(-1);
         at = str_index_of_cstr_range(src, marker, at + 1))
    {
        // 경계: 맨 앞 · ';' 뒤 · 값 없는 G1 뒤(G1G2) · 앞 패킷 끝 G5 뒤(세척관리는 ';' 없이 G5 로 끝난다). 'G+숫자' 일반화는
        // 값 "G3G1234" 를 다시 잡으므로 금지.
        if (at == 0 || src[at - 1] == ';' ||
            (at >= 2 && src[at - 2] == 'G' && (src[at - 1] == '1' || src[at - 1] == '5'))) return at;
    }
    return static_cast<size_t>(-1);
}

bool GatewayProcessor::find_string(const char *src, char *dst, size_t dstSize, const char *from, const char *to)
{
    if (src == nullptr || dst == nullptr || from == nullptr || to == nullptr) return false;
    // 한 레코드 안에서는 첫 경계 일치. 여러 레코드는 호출 전에 마지막 레코드로 좁혀 둔다.
    const auto fromIdx = find_marker(src, from, 0);
    if (fromIdx == static_cast<size_t>(-1)) return false;
    const auto toIdx = find_marker(src, to, fromIdx + str_strlen(from));
    if (toIdx == static_cast<size_t>(-1)) return false;
    str_substring_safe(src, dst, dstSize, fromIdx + str_strlen(from), toIdx);
    return true;
}

// [5차 판정 · 재론 금지] G1 본체번호는 7자리까지(8자리 잘림) · 검사명 파서의 48 은 "3×16" 뜻의 절대 인덱스 —
//  알려진 송신자(델파이·세척관리·SeePro) 는 어느 쪽도 보내지 않는다. 바꾸면 wire 호환 검증이 다시 필요.
bool GatewayProcessor::substring_for_patient(const char *string)
{
    // [17] = 블록 16바이트를 다 담기 위한 크기(str_substring_safe 는 NUL 자리를 남긴다).
    // 15바이트만 담던 때는 올눈이 보내는 16바이트 조각의 끝 한글이 반쪽으로 남았다 (09-23).
    char data[17]{};
    const auto keyIdx = str_index_of(string, ';');
    if (keyIdx == static_cast<size_t>(-1)) return false;

    str_substring_safe(string, data, sizeof(data), 0, keyIdx);
    if (str_strlen(data) == 0) return false;
    memcpy(mPatientKey, data, sizeof(mPatientKey));

    const auto nameIdx = str_index_of_range(string, ';', keyIdx + 1);
    if (nameIdx != static_cast<size_t>(-1))
    {
        memset(data, 0, sizeof(data));
        str_substring_safe(string, data, sizeof(data), keyIdx + 1, nameIdx);
        memcpy(mPatientName, data, sizeof(mPatientName));
    }
    return true;
}

void GatewayProcessor::substring_for_examination_subject(const char *string)
{
    memset(mExaminationSubject,  0, 16);
    memset(mExaminationSubject2, 0, 16);
    memset(mExaminationSubject3, 0, 16);

    char data[17]{};   // 블록 16바이트 전부 (위 substring_for_patient 주석 참조)
    const auto firstIdx = str_index_of_cstr(string, ";");
    if (firstIdx == static_cast<size_t>(-1)) return;
    str_substring_safe(string, data, sizeof(data), 0, firstIdx);
    if (str_strlen(data) != 0) memcpy(mExaminationSubject, data, 16);

    memset(data, 0, sizeof(data));
    const auto secondIdx = str_index_of_range(string, ';', firstIdx + 1);
    if (secondIdx != static_cast<size_t>(-1))
    {
        str_substring_safe(string, data, sizeof(data), firstIdx + 1, secondIdx);
        if (str_strlen(data) != 0) memcpy(mExaminationSubject2, data, 16);
    }

    memset(data, 0, sizeof(data));
    const auto thirdIdx = (secondIdx != static_cast<size_t>(-1))
        ? str_index_of_range(string, ';', secondIdx + 1)
        : static_cast<size_t>(-1);
    if (thirdIdx != static_cast<size_t>(-1))
    {
        str_substring_safe(string, data, sizeof(data), secondIdx + 1, thirdIdx);
    }
    else if (str_strlen(string) > 48 && secondIdx != static_cast<size_t>(-1))
    {
        str_substring_safe(string, data, sizeof(data), secondIdx + 1, 48);
    }
    if (str_strlen(data) != 0) memcpy(mExaminationSubject3, data, 16);
}

void GatewayProcessor::substring_for_local_date_time(char *string)
{
    mDateTime = LocalDateTime{};

    const int year   = str_atoi(strtok(string, ";"));
    if (year   == -1) return;
    const int month  = str_atoi(strtok(nullptr, ";"));
    if (month  == -1) return;
    const int day    = str_atoi(strtok(nullptr, ";"));
    if (day    == -1) return;
    if (strtok(nullptr, ";") == nullptr) return;
    const int hour   = str_atoi(strtok(nullptr, ";"));
    if (hour   == -1) return;
    const int minute = str_atoi(strtok(nullptr, ";"));
    if (minute == -1) return;
    const int second = str_atoi(strtok(nullptr, ";"));
    if (second == -1) return;
    // 범위 밖(월 13·시 25 …)은 검사일시 없음으로 — RTC 를 엉뚱한 해로 맞추지 않는다(5차 D).
    if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour > 23 || minute > 59 || second > 59) return;

    mDateTime = LocalDateTime{
        LocalDate{static_cast<uint16_t>(year), static_cast<uint8_t>(month), static_cast<uint8_t>(day)},
        LocalTime{static_cast<uint8_t>(hour), static_cast<uint8_t>(minute), static_cast<uint8_t>(second)}};
}

void GatewayProcessor::print_to_allnun(uint8_t sectorTrailer, uint8_t block, unsigned char *buffer)
{
    if (sectorTrailer < 0x10) Serial.print('0');
    Serial.print(sectorTrailer, HEX);
    if (block < 0x10) Serial.print('0');
    Serial.print(block, HEX);
    for (uint8_t i = 0; i < 16; ++i)
    {
        if (buffer[i] < 0x10) Serial.print('0');
        Serial.print(buffer[i], HEX);
    }
    Serial.println(';');
}
