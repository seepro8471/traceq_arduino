#pragma once

// TraceQ Arduino 2.0 — 우산 헤더.

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <avr/pgmspace.h>

// READER_MODE를 활성화하면 본래 기능 대신 접촉한 태그의 데이터를 단순 출력만.
// #define READER_MODE

#include "TraceQ_Arduino/version.hpp"

// data
#include "TraceQ_Arduino/data/PinMap.hpp"
#include "TraceQ_Arduino/data/BlockMap.hpp"
#include "TraceQ_Arduino/data/Company.hpp"
#include "TraceQ_Arduino/data/System.hpp"
#include "TraceQ_Arduino/data/Tag.hpp"
#include "TraceQ_Arduino/data/Process.hpp"
#include "TraceQ_Arduino/data/Gateway.hpp"
#include "TraceQ_Arduino/data/Patient.hpp"
#include "TraceQ_Arduino/data/LocalDateTime.hpp"
#include "TraceQ_Arduino/data/WashingRecord.hpp"
#include "TraceQ_Arduino/data/DisinfectionRecord.hpp"

// nvm
#include "TraceQ_Arduino/data/nvm/AlarmOption.hpp"
#include "TraceQ_Arduino/data/nvm/DeviceOption.hpp"
#include "TraceQ_Arduino/data/nvm/DisinfectionOption.hpp"
#include "TraceQ_Arduino/data/nvm/ManagerOption.hpp"
#include "TraceQ_Arduino/data/nvm/RecordOption.hpp"

// avr utils
#include "TraceQ_Arduino/avr/AvrString.hpp"
#include "TraceQ_Arduino/avr/AvrUtil.hpp"

// rfid (재설계됨)
#include "TraceQ_Arduino/rfid/RfidConfig.hpp"
#include "TraceQ_Arduino/rfid/RfidController.hpp"

// rtc / ui — 1.0 verbatim.
#include "TraceQ_Arduino/module/rtc/DefaultRtc.hpp"
#include "TraceQ_Arduino/ui/LcdPrinter.hpp"

#ifndef READER_MODE
#include "TraceQ_Arduino/BaseProcessor.hpp"
#include "TraceQ_Arduino/RecordProcessor.hpp"
#include "TraceQ_Arduino/WashingProcessor.hpp"
#include "TraceQ_Arduino/DisinfectionProcessor.hpp"
#include "TraceQ_Arduino/GatewayProcessor.hpp"
#include "TraceQ_Arduino/SerialProcessor.hpp"
#else
#include "TraceQ_Arduino/SimpleScanner.hpp"
#endif

// 1.0과 동일하게 READER_MODE에 따라 UI 클래스 별칭 전환.
#ifndef UserInterfaceClass
  #ifdef READER_MODE
    #include "TraceQ_Arduino/ui/reader/ReaderUserInterface.hpp"
    #define UserInterfaceClass ReaderUserInterface
  #else
    #include "TraceQ_Arduino/ui/UserInterface.hpp"
    #define UserInterfaceClass UserInterface
  #endif
#endif
