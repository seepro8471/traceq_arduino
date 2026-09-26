#pragma once

#include "BaseUserInterface.hpp"
#include "TraceQ_Arduino/version.hpp"

#include "TraceQ_Arduino/data/System.hpp"
#include "TraceQ_Arduino/data/nvm/AlarmOption.hpp"
#include "TraceQ_Arduino/data/nvm/DeviceOption.hpp"
#include "TraceQ_Arduino/data/nvm/DisinfectionOption.hpp"
#include "TraceQ_Arduino/data/nvm/ManagerOption.hpp"
#include "TraceQ_Arduino/data/nvm/RecordOption.hpp"
#include "TraceQ_Arduino/module/rtc/DefaultRtc.hpp"

/**
 * \class UserInterface
 * \extends BaseUserInterface
 * \brief Home 화면, 메뉴 등 사용자의 TraceQ Arduino 사용을 돕기 위한 함수를 구현하는 클래스.
 *
 * \since 1.2
 */
class UserInterface : public BaseUserInterface
{

public:
    /**
     * 메뉴 동작의 결과로 사용될 enum class. 메뉴가 enum의 순서대로 표시될 것이라는 보장은 없다.
     *
     * \enum MenuFunction
     */
    enum class MenuFunction
    {

        Prev = 0,

        Home,

        Next,

        Exit,

        Save,

        // default

        Date,

        Time,

        Type,

        Number,

        // record

        AlarmFlag,

        AlarmTimeSlot,

        PatientCheck,

        ManagerDisposability,

        // disinfection

        MaximumCount,

        GroupDelay,

        Range,

    };

    explicit UserInterface(DisplayClass &crystal) : BaseUserInterface(crystal){};

    void UserInterfaceInitialize(char deviceType) override;

    /**
     * \brief Home 화면을 표시한다.
     *
     * \param rtc 현재 시간을 출력하기 위한 RtcImpl
     * \param deviceNumber 현재 기기의 번호
     */
    void DisplayHome(DefaultRtc &rtc, int deviceNumber);

    /// 화면을 지운 뒤(메뉴 진입/복귀 등) 다음 DisplayHome·InfoRow1·InfoReader 이 전부 다시 그리도록.
    void InvalidateHome();

    /// 홈 1행(알람/연결 상태) — 내용이 바뀌었을 때만 다시 그린다(표시 결과 동일).
    void InfoRow1(const __FlashStringHelper *string);
    void InfoRow1_cstr(const char *string);
    /// RFID 상태 표시(R-O/R-X) — 상태가 바뀔 때만.
    void InfoReader(bool alive);

    /**
     * \brief 인자로 넘어온 인덱스의 메뉴를 표시하고 해당 메뉴에서 선택한 기능(eFuntion)을/를 반환한다.
     *
     * \param index 표시할 메뉴의 인덱스
     * \return 표시한 메뉴에서 선택한 기능
     */
    MenuFunction DisplayMenu(uint8_t index = 0);

    /**
     * \brief 마지막으로 표시된 메뉴의 다음 메뉴를 표시한다.
     *
     * \details 만약 마지막으로 표시된 메뉴가 등록된 메뉴 중 '마지막'이었을 경우, 0번 인덱스의 메뉴를 표시한다.
     *
     * \return 표시한 메뉴에서 선택한 기능
     */
    MenuFunction DisplayNextMenu();

    /**
     * \brief 마지막으로 표시된 메뉴의 이전 메뉴를 표시한다.
     *
     * \details 만약 마지막으로 표시된 메뉴가 등록된 메뉴 중 '처음'이었을 경우, 마지막 인덱스의 메뉴를 표시한다.
     *
     * \return 표시한 메뉴에서 선택한 기능
     */
    MenuFunction DisplayPrevMenu();

    // device option

    MenuFunction SetDeviceDate(DefaultRtc &rtc);

    MenuFunction SetDeviceTime(DefaultRtc &rtc);

    MenuFunction SetDeviceType(DeviceOption &option);

    MenuFunction SetDeviceNumber(DeviceOption &option);

    // record option

    MenuFunction SetRecordAlarmFlag(AlarmOption &option);

    MenuFunction SetRecordAlarmTimeSlot(char deviceType, AlarmOption &option);

    MenuFunction SetRecordPatientCheck(RecordOption &option);

    MenuFunction SetRecordManagerDisposability(RecordOption &option);

    // disinfection option

    MenuFunction SetDisinfectionMaximumCount(DisinfectionOption &option);

    MenuFunction SetDisinfectionGroupDelay(DisinfectionOption &option);

    MenuFunction SetDisinfectionRange(DisinfectionOption &option);

protected:
    // 메뉴 무조작 시한(사장님 09-25): 60초 동안 아무 버튼도 안 누르면 저장 없이 홈으로 나간다.
    // 메뉴에 머무는 동안은 태그를 전혀 읽지 않으므로, 열어 두고 자리를 비우거나 버튼이 붙어 고장나도
    // 기기가 영영 멈추지 않게. 눌린 버튼을 읽을 때마다 활동 시각을 갱신한다.
    static constexpr unsigned long kMenuIdleMs{60000UL};
    unsigned long mMenuActiveAt{0};
    inline void menu_touch() { mMenuActiveAt = millis(); }
    inline bool menu_idle_expired() const { return millis() - mMenuActiveAt >= kMenuIdleMs; }

    // ★활동 갱신은 '눌린 채' 가 아니라 **누르는 순간(HIGH→LOW 에지)** 에만 — 붙은 채 고장난 버튼이 시한을
    //  영영 미루던 것(5차 A2). 눌린 동안 반환값은 종전과 같다(메뉴 조작 불변).
    uint8_t mBtnHeld{0};   // bit0=SELECT bit1=LEFT bit2=RIGHT
    inline bool read_button_edge(uint8_t pin, uint8_t bit)
    {
        const bool low = digitalRead(pin) == LOW;
        const bool was = (mBtnHeld & bit) != 0;
        if (low && !was) menu_touch();
        if (low) mBtnHeld |= bit; else mBtnHeld &= static_cast<uint8_t>(~bit);
        return low;
    }
    inline bool read_select_button() { return read_button_edge(PIN_SELECT_BUTTON, 0x01); }
    inline bool read_left_button()   { return read_button_edge(PIN_LEFT_BUTTON,   0x02); }
    inline bool read_right_button()  { return read_button_edge(PIN_RIGHT_BUTTON,  0x04); }

    /**
     * Line과 option navigation을 표시한다.
     *
     * \param line 표시할 line
     */
    void display_line(const Line &line);

    /**
     * Screen과 menu navigation을 표시한다.
     *
     * \param screen 표시할 screen
     */
    void display_screen(const Screen &screen);

    /**
     * \brief line에 저장된(content) 정수 문자를 조정한다. 결과값은 line에 저장된다.
     *
     * \param line 원본 데이터를 포함한 Line
     * \param title 제목 문자열
     * \return 옵션의 실행 결과
     */
    MenuFunction edit_number(Line &line, const char *title);

    /**
     * \brief line에 저장된(content) 정수 문자를 조정한다. 결과값은 line에 저장된다.
     *
     * \param line 원본 데이터를 포함한 Line
     * \param title 제목 문자열
     * \return 옵션의 실행 결과
     */
    MenuFunction edit_number(Line &line, const __FlashStringHelper *title);

    /**
     * \brief 넘겨받은 parameter 중 하나를 선택하여 line에 저장한다.
     *
     * \param line 원본 데이터를 포함한 Line
     * \param title 제목 문자열
     * \param p1 parameter 1
     * \param p2 parameter 2
     * \return 옵션의 실행 결과
     */
    MenuFunction select(Line &line, const char *title, const char *p1, const char *p2, uint8_t start = 0);

    /**
     * \brief 넘겨받은 parameter 중 하나를 선택하여 line에 저장한다.
     *
     * \param line 원본 데이터를 포함한 Line
     * \param title 제목 문자열
     * \param p1 parameter 1
     * \param p2 parameter 2
     * \return 옵션의 실행 결과
     */
    MenuFunction select(Line &line, const __FlashStringHelper *title, const char *p1, const char *p2, uint8_t start = 0);

    /**
     * \brief 넘겨받은 parameter 중 하나를 선택하여 line에 저장한다.
     *
     * \param line 원본 데이터를 포함한 Line
     * \param title 제목 문자열
     * \param p1 parameter 1
     * \param p2 parameter 2
     * \param p3 parameter 3
     * \param p4 parameter 4
     * \return 옵션의 실행 결과
     */
    MenuFunction select(Line &line, const char *title, const char *p1, const char *p2,
                        const char *p3, const char *p4, uint8_t start = 0);

    /**
     * \brief 넘겨받은 parameter 중 하나를 선택하여 line에 저장한다.
     *
     * \param line 원본 데이터를 포함한 Line
     * \param title 제목 문자열
     * \param p1 parameter 1
     * \param p2 parameter 2
     * \param p3 parameter 3
     * \param p4 parameter 4
     * \return 옵션의 실행 결과
     */
    MenuFunction select(Line &line, const __FlashStringHelper *title, const char *p1, const char *p2,
                        const char *p3, const char *p4, uint8_t start = 0);

private:
    /**
     * \brief 타입의 ui를 구성한다.
     */
    void gateway_ui_initialize();

    /**
     * \brief 세척 타입의 ui를 구성한다.
     */
    void washing_ui_initialize();

    /**
     * \brief 소독 타입 ui를 구성한다.
     */
    void disinfection_ui_initialize();

    /**
     * \brief 서버 타입 ui를 구성한다.
     */
    void server_ui_initialize();

    /**
     * \brief line에 저장된(content) 정수 문자를 조정한다. 결과값은 line에 저장됨.
     *
     * \param line 원본 데이터를 포함한 Line
     * \return 옵션의 실행 결과
     */
    MenuFunction edit_number_impl(Line &line);

    /**
     * \brief 넘겨받은 parameter 중 하나를 선택하여 line에 저장한다.
     *
     * \param line 원본 데이터를 포함한 Line
     * \param p1 parameter 1
     * \param p2 parameter 2
     * \return 옵션의 실행 결과
     */
    MenuFunction select_impl(Line &line, const char *p1, const char *p2, uint8_t start);

    /**
     * \brief 넘겨받은 parameter 중 하나를 선택하여 line에 저장한다.
     *
     * \param line 원본 데이터를 포함한 Line
     * \param p1 parameter 1
     * \param p2 parameter 2
     * \param p3 parameter 3
     * \param p4 parameter 4
     */
    MenuFunction select_impl(Line &line, const char *p1, const char *p2,
                             const char *p3, const char *p4, uint8_t start);

    /**
     * \brief 0 ~ 9 사이의 정수를 저장하는 문자를 parameter decrement의 값에 따라 증감시킨다.
     *
     * \details 이 함수는 오직 0 ~ 9 사이의 숫자만을 다루므로 increment 상태로 넘어온 문자가 9였을 경우, 10이 아닌 0으로 변환한다.
     * 마찬가지로 decrement 상태에서 넘어온 문자 0는 9로 변환됨.
     *
     * \param outChar 0 ~ 9 사이의 정수 문자
     * \param decrement 증감 연산 플래그
     */
    void edit_uint8t_char(char *outChar, bool decrement = false);

private:
    /**
     * \brief 현재 시각을 출력하기 위한 버퍼. Home 화면 구성에 사용된다.
     */
    char mDateTimeBuffer[20]{};

    /**
     * \brief 현재 기기의 타입과 번호를 출력하기 위한 버퍼. Home 화면 구성에 사용된다.
     *
     * \details col 16, row 3에 표시된다.
     */
    // "%c:%02d" — device_number는 JSON으로 3자리 이상 설정될 수 있으므로
    // int16 최대(5자리)까지 수용 (1.0은 [5]라 3자리부터 오버런).
    char mDeviceInfoBuffer[8]{};

    // Home 화면 갱신 최소화용 캐시 (2.2.5) — 값이 바뀐 항목만 LCD 에 다시 쓴다.
    char mHomeShownDateTime[20]{};
    int  mHomeShownNumber{-1};
    bool mHomeStaticShown{false};
    // 1행(알람/상태)·R-O 표시도 매 루프 다시 쓰면 그만큼 I2C 지연이다 — 바뀐 것만 그린다.
    // ★InvalidateHome 이 이 캐시도 지운다(메뉴·경고로 화면을 지운 뒤 반드시 다시 그리게).
    char mHomeShownRow1[21]{};
    char mHomeShownReader{0};

    /// \brief 어떠한 컨텐츠를 '선택'한 다음의 딜레이.
    unsigned long mInterval{300};

    // option lines

    Line mDeviceNumber{static_cast<uint8_t>(MenuFunction::Number), "Number"};

    Line mDeviceType{static_cast<uint8_t>(MenuFunction::Type), "Type"};

    Line mDeviceDate{static_cast<uint8_t>(MenuFunction::Date), "Date"};

    Line mDeviceTime{static_cast<uint8_t>(MenuFunction::Time), "Time"};

    Line mAlarmSoundFlag{static_cast<uint8_t>(MenuFunction::AlarmFlag), "Sound"};

    Line mAlarmSlot{static_cast<uint8_t>(MenuFunction::AlarmTimeSlot), "Alarm"};

    Line mPatientCheck{static_cast<uint8_t>(MenuFunction::PatientCheck), "P-Check"};

    Line mManagerDisposability{static_cast<uint8_t>(MenuFunction::ManagerDisposability), "M-Check"};

    Line mDisinfectionMaximumCount{static_cast<uint8_t>(MenuFunction::MaximumCount), "MaxCount"};

    Line mDisinfectionGroupDelay{static_cast<uint8_t>(MenuFunction::GroupDelay), "Delay"};

    Line mDisinfectionRange{static_cast<uint8_t>(MenuFunction::Range), "Range"};

    // option screens

    Screen mOptionSlot1{};

    Screen mOptionSlot2{};

    Screen mOptionSlot3{};

    // option menu

    Menu mOptionMenu{mOptionSlot1};
};
