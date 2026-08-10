#pragma once

#ifndef DisplayClass

#define Crystal_I2C

#ifdef Crystal

#include <LiquidCrystal.h>

#define DisplayClass LiquidCrystal

#endif

#ifdef Crystal_I2C

#include <LiquidCrystal_I2C.h>

#define DisplayClass LiquidCrystal_I2C

#endif

#endif

constexpr uint8_t MAX_LINES = 4;

constexpr uint8_t MAX_SCREENS = 8;

constexpr char EMPHASIS_OFF = 32;

constexpr char EMPHASIS_ON = 42;