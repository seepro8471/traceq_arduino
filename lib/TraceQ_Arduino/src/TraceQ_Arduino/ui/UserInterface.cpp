#include "UserInterface.hpp"

void UserInterface::UserInterfaceInitialize(const char deviceType)
{
    mType = deviceType;

    // general option
    // 게이트웨이도 번호 메뉴를 유지한다 — PC 가 `0000`(지정 없음)을 주면
    // 이 기기 번호가 태그에 기록되기 때문 (2.2.6 규칙).
    mOptionSlot1.AddLine(mDeviceNumber);
    mOptionSlot1.AddLine(mDeviceType);

    switch (mType)
    {
    case WASHING_TYPE_DEVICE:
    {
        washing_ui_initialize();
        break;
    }
    case DISINFECTION_TYPE_DEVICE:
    {
        disinfection_ui_initialize();
        break;
    }
    case SERVER_TYPE_DEVICE:
    {
        server_ui_initialize();
        break;
    }
    default:
    {
        gateway_ui_initialize();
        break;
    }
    }
    mLcd.init();
    mLcd.backlight();
}

void UserInterface::gateway_ui_initialize()
{
    // slot 1
    mOptionSlot1.AddLine(mDeviceDate);
    mOptionSlot1.AddLine(mDeviceTime);
}

void UserInterface::washing_ui_initialize()
{
    // slot 1
    mOptionSlot1.AddLine(mAlarmSlot);
    mOptionSlot1.AddLine(mAlarmSoundFlag);

    // slot 2
    mOptionSlot2.AddLine(mPatientCheck);
    mOptionSlot2.AddLine(mManagerDisposability);
    mOptionSlot2.AddLine(mDeviceDate);
    mOptionSlot2.AddLine(mDeviceTime);

    // add slot
    mOptionMenu.AddScreen(mOptionSlot2);
}

void UserInterface::disinfection_ui_initialize()
{
    // slot 1
    mOptionSlot1.AddLine(mAlarmSlot);
    mOptionSlot1.AddLine(mAlarmSoundFlag);

    // slot 2
    mOptionSlot2.AddLine(mPatientCheck);
    mOptionSlot2.AddLine(mManagerDisposability);
    mOptionSlot2.AddLine(mDeviceDate);
    mOptionSlot2.AddLine(mDeviceTime);

    // slot 3
    mOptionSlot3.AddLine(mDisinfectionRange);
    mOptionSlot3.AddLine(mDisinfectionGroupDelay);
    mOptionSlot3.AddLine(mDisinfectionMaximumCount);

    // add slot
    mOptionMenu.AddScreen(mOptionSlot2);
    mOptionMenu.AddScreen(mOptionSlot3);
}

void UserInterface::server_ui_initialize()
{
    // slot 1
    mOptionSlot1.AddLine(mDeviceDate);
    mOptionSlot1.AddLine(mDeviceTime);
    // (2.2.0: Version(Latest/Old) 메뉴 삭제 — 서버는 레거시 단일 경로)
}

void UserInterface::DisplayHome(DefaultRtc &rtc, const int deviceNumber)
{
    // ★loop 마다 51자를 I2C 로 다시 쓰면 그 자체가 루프의 최대 지연원이 된다
    //  (100kHz I2C 기준 문자당 ~1.6ms). 내용이 바뀐 항목만 갱신한다 — 표시
    //  결과는 동일하고 루프 응답성만 개선 (2.2.5).
    //  시각은 초 단위로 바뀌므로 실질적으로 초당 1회만 다시 쓰인다.
    const char *now = rtc.ToInternalString(mDateTimeBuffer);
    if (strncmp(now, mHomeShownDateTime, sizeof(mHomeShownDateTime) - 1) != 0)
    {
        strncpy(mHomeShownDateTime, now, sizeof(mHomeShownDateTime) - 1);
        mHomeShownDateTime[sizeof(mHomeShownDateTime) - 1] = '\0';
        mLcd.setCursor(0, 0);
        mLcd.print(mHomeShownDateTime);
    }

    // 버전·기기정보는 부팅 후 바뀌지 않는다(타입 변경 시 재시작) — 1회만 출력.
    if (!mHomeStaticShown)
    {
        mLcd.setCursor(6, 3);
        mLcd.print(TRACEQ_ARDUINO_VERSION);
        mHomeStaticShown = true;
        mHomeShownNumber = deviceNumber + 1;   // 아래에서 반드시 그려지도록
    }
    if (mHomeShownNumber != deviceNumber)
    {
        mHomeShownNumber = deviceNumber;
        snprintf(mDeviceInfoBuffer, sizeof(mDeviceInfoBuffer), "%c:%02d", mType, deviceNumber);
        mLcd.setCursor(16, 3);
        mLcd.print(mDeviceInfoBuffer);
    }
}

void UserInterface::InvalidateHome()
{
    // 메뉴·경고 등으로 화면을 지운 뒤에는 다음 DisplayHome 이 전부 다시 그리게 한다.
    mHomeShownDateTime[0] = '\0';
    mHomeStaticShown = false;
}

UserInterface::MenuFunction UserInterface::DisplayMenu(uint8_t index)
{
    // get screen and print
    const Screen &s = mOptionMenu.GetScreen(index);
    display_screen(s);

    // loop
    while (true)
    {
        if (read_select_button())
        {
            switch (compare_position())
            {
            case Position::Equal:
            {
                return MenuFunction::Next;
            }
            case Position::Lower1:
            {
                return MenuFunction::Home;
            }
            case Position::Lower2:
            {
                return MenuFunction::Prev;
            }
            default:
            {
                // uint8_t to eFuntion
                return (MenuFunction)s[getCurrentPosition()].GetUniqueVar();
            }
            }
        }

        if (read_left_button())
        {
            adjust_position(true);
            emphasis_on_screen(s);
            // interval
            delay(mInterval);
        }

        if (read_right_button())
        {
            adjust_position();
            emphasis_on_screen(s);
            // interval
            delay(mInterval);
        }
    }
}

UserInterface::MenuFunction UserInterface::DisplayNextMenu()
{
    auto current{mOptionMenu.GetCurrentScreen()};
    auto maximum{mOptionMenu.GetTotalScreen() - 1};
    if (current == maximum)
    {
        current = 0;
    }
    else
    {
        current++;
    }
    return DisplayMenu(current);
}

UserInterface::MenuFunction UserInterface::DisplayPrevMenu()
{
    auto current{mOptionMenu.GetCurrentScreen()};
    auto maximum{mOptionMenu.GetTotalScreen() - 1};
    if (current == 0)
    {
        current = maximum;
    }
    else
    {
        current--;
    }
    return DisplayMenu(current);
}

void UserInterface::display_line(const Line &line)
{
    mLcd.clear();
    InvalidateHome();   // 화면을 지웠으므로 홈 복귀 시 전체 재출력
    // print line and option navigation
    print_line(line);
    print_option_navigation(2, 3);
    // set position
    setCurrentPosition(0);
    setMaximumPosition(str_strlen(line.GetContent()) + 1);
    // emphasis
    emphasis_on_line(line);
}

void UserInterface::display_screen(const Screen &screen)
{
    mLcd.clear();
    InvalidateHome();   // 화면을 지웠으므로 홈 복귀 시 전체 재출력
    // print screen and menu navigation
    print_screen(screen);
    print_menu_navigation();
    // set position
    setCurrentPosition(0);
    setMaximumPosition(screen.GetLineCount() + 2);
    // emphasis
    emphasis_on_screen(screen);
}

UserInterface::MenuFunction UserInterface::edit_number(Line &line, const char *title)
{
    // print line and title
    display_line(line);
    print_title(title);

    return edit_number_impl(line);
}

UserInterface::MenuFunction UserInterface::edit_number(Line &line, const __FlashStringHelper *title)
{
    // print line and title
    display_line(line);
    print_title(title);

    return edit_number_impl(line);
}

UserInterface::MenuFunction UserInterface::select(Line &line, const char *title, const char *p1, const char *p2)
{
    // print option and title
    print_option_nagivation();
    print_title(title);

    return select_impl(line, p1, p2);
}

UserInterface::MenuFunction UserInterface::select(Line &line, const __FlashStringHelper *title,
                                                  const char *p1, const char *p2)
{
    // print option and title
    print_option_nagivation();
    print_title(title);

    return select_impl(line, p1, p2);
}

UserInterface::MenuFunction UserInterface::select(Line &line, const char *title, const char *p1, const char *p2,
                                                  const char *p3, const char *p4)
{
    // print option and title
    print_option_nagivation();
    print_title(title);

    return select_impl(line, p1, p2, p3, p4);
}

UserInterface::MenuFunction UserInterface::select(Line &line, const __FlashStringHelper *title, const char *p1,
                                                  const char *p2, const char *p3, const char *p4)
{
    // print option and title
    print_option_nagivation();
    print_title(title);

    return select_impl(line, p1, p2, p3, p4);
}

UserInterface::MenuFunction UserInterface::edit_number_impl(Line &line)
{
    // var
    char *content{line.GetContent()};
    bool editable{false};

    // loop
    while (true)
    {
        if (editable)
        {
            mLcd.setCursor(16, 0);
            mLcd.print(F("Edit"));
        }
        else
        {
            mLcd.setCursor(16, 0);
            mLcd.print(F("    "));
        }

        if (read_select_button())
        {
            if (editable)
            {
                editable = false;
            }
            else
            {
                switch (compare_position())
                {
                case BaseUserInterface::Position::Equal:
                {
                    return MenuFunction::Save;
                }
                case BaseUserInterface::Position::Lower1:
                {
                    return MenuFunction::Exit;
                }
                default:
                {
                    editable = true;
                    break;
                }
                }
            } // if (editable)
            delay(mInterval);
        }

        if (read_left_button())
        {
            if (editable)
            {
                edit_uint8t_char(content, true);
                print_line(line);
            }
            else
            {
                adjust_position(true);
                emphasis_on_line(line);
            }
            delay(mInterval);
        }

        if (read_right_button())
        {
            if (editable)
            {
                edit_uint8t_char(content);
                print_line(line);
            }
            else
            {
                adjust_position();
                emphasis_on_line(line);
            }
            delay(mInterval);
        }
    }
}

UserInterface::MenuFunction UserInterface::select_impl(Line &line, const char *p1, const char *p2)
{
    // print parameter
    mLcd.setCursor(3, 1);
    mLcd.print(p1);

    // parameter position
    uint8_t currentParam{0};
    uint8_t maximumParam{1};

    // copy
    const char *var{p1};

    // loop
    while (true)
    {
        if (read_select_button())
        {
            switch (compare_position())
            {
            case Position::Equal:
            {
                line.SetContent(var);
                return MenuFunction::Save;
            }
            case Position::Lower1:
            {
                return MenuFunction::Exit;
            }
            case Position::Lower2:
            {
                if (currentParam == maximumParam)
                {
                    currentParam = 0;
                }
                else
                {
                    currentParam++;
                }
                break;
            }
            case Position::LowerN:
            {
                if (currentParam == 0)
                {
                    currentParam = maximumParam;
                }
                else
                {
                    currentParam--;
                }
                break;
            }
            default:
            {
                util_soft_reset();
            }
            } // switch (comparePosition())

            // 선택된 paramter 변경
            switch (currentParam)
            {
            case 0:
            {
                var = p1;

                break;
            }
            case 1:
            {
                var = p2;

                break;
            }
            default:
            {
                // bug
                util_soft_reset();
            }
            }

            // print selected param
            mLcd.setCursor(3, 1);
            mLcd.print("              ");
            mLcd.setCursor(3, 1);
            mLcd.print(var);

            delay(mInterval);
        }

        if (read_left_button())
        {
            adjust_position(true);
            emphasis_on_option_navigation();

            delay(mInterval);
        }

        if (read_right_button())
        {
            adjust_position();
            emphasis_on_option_navigation();

            delay(mInterval);
        }
    }
}

UserInterface::MenuFunction UserInterface::select_impl(Line &line, const char *p1, const char *p2,
                                                       const char *p3, const char *p4)
{
    // print parameter
    mLcd.setCursor(3, 1);
    mLcd.print(p1);

    // parameter position
    uint8_t currentParam{0};
    uint8_t maximumParam{3};

    // copy
    const char *var{p1};

    // loop
    while (true)
    {
        if (read_select_button())
        {
            switch (compare_position())
            {
            case Position::Equal:
            {
                line.SetContent(var);
                return MenuFunction::Save;
            }
            case Position::Lower1:
            {
                return MenuFunction::Exit;
            }
            case Position::Lower2:
            {
                if (currentParam == maximumParam)
                {
                    currentParam = 0;
                }
                else
                {
                    currentParam++;
                }
                break;
            }
            case Position::LowerN:
            {
                if (currentParam == 0)
                {
                    currentParam = maximumParam;
                }
                else
                {
                    currentParam--;
                }
                break;
            }
            default:
            {
                util_soft_reset();
            }
            } // switch (comparePosition())

            // 선택된 paramter 변경
            switch (currentParam)
            {
            case 0:
            {
                var = p1;

                break;
            }
            case 1:
            {
                var = p2;

                break;
            }
            case 2:
            {
                var = p3;

                break;
            }
            case 3:
            {
                var = p4;

                break;
            }
            default:
            {
                break;
            }
            }

            // print selected param
            mLcd.setCursor(3, 1);
            mLcd.print("              ");
            mLcd.setCursor(3, 1);
            mLcd.print(var);

            delay(mInterval);
        }

        if (read_left_button())
        {
            adjust_position(true);
            emphasis_on_option_navigation();

            delay(mInterval);
        }

        if (read_right_button())
        {
            adjust_position();
            emphasis_on_option_navigation();

            delay(mInterval);
        }
    }
}

void UserInterface::edit_uint8t_char(char *outChar, bool decrement)
{
    uint8_t number = outChar[getCurrentPosition()] - '0';
    if (decrement)
    {
        if (number == 0)
        {
            number = 9;
        }
        else
        {
            number--;
        }
    }
    else
    { // increment
        if (number == 9)
        {
            number = 0;
        }
        else
        {
            number++;
        }
    }
    outChar[getCurrentPosition()] = str_to_char(number);
}
