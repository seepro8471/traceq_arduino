#!/bin/bash
# 사용: build.sh <펌웨어 루트> <출력 폴더> <시험.cpp>...
# 제품 소스(src/main.cpp + lib/TraceQ_Arduino/src 전부)를 실물 헤더·실물 빌드 플래그로 1회 컴파일하고,
# 하드웨어에 닿는 함수만 fakes/ 의 가짜 정의로 링크한다. 시험마다 <출력 폴더>/<이름>.elf.
set -e
ROOT="$1"; OUTD="$2"; shift 2
HERE="$(cd "$(dirname "$0")" && pwd)"
TC="/c/Users/alu5/.platformio/packages/toolchain-atmelavr/bin"
FW="/c/Users/alu5/.platformio/packages/framework-arduino-avr"
LD="/d/traceq_arduino 2.0/.pio/libdeps/megaatmega2560"
OBJ="$OUTD/obj"; rm -rf "$OBJ"; mkdir -p "$OBJ"

DEFS="-DARDUINO=10808 -DARDUINO_AVR_MEGA2560 -DARDUINO_ARCH_AVR -DF_CPU=16000000L
      -DSERIAL_RX_BUFFER_SIZE=512 -DTRACEQ_RFID_VERIFY_WRITES=1 -DTRACEQ_RFID_MAX_WRITE_RETRIES=3
      -DMFRC522_SPICLOCK=1000000UL"
INC=(-I"$HERE/fakes" -I"$HERE/tests" -I"$FW/cores/arduino" -I"$FW/variants/mega"
     -I"$FW/libraries/EEPROM/src" -I"$FW/libraries/SPI/src" -I"$FW/libraries/Wire/src"
     -I"$LD/ArduinoJson/src" -I"$LD/RTClib/src" -I"$LD/Adafruit BusIO" -I"$LD/LiquidCrystal_I2C"
     -I"$ROOT/lib/MFRC522" -I"$ROOT/lib/TraceQ_Arduino/src")
CXXF="-mmcu=atmega2560 -Os -g -std=gnu++11 -fno-exceptions -fno-threadsafe-statics
      -ffunction-sections -fdata-sections -w"
LDF=""
# 제품과 같은 LTO(PlatformIO atmelavr 기본). LTO=0 이면 끈다.
if [ "${LTO:-1}" = "1" ]; then CXXF="$CXXF -flto -fno-fat-lto-objects"; LDF="-flto -fuse-linker-plugin"; fi
CF="-mmcu=atmega2560 -Os -g -ffunction-sections -fdata-sections -w"

n=0
cxx() { n=$((n+1)); "$TC/avr-g++" $CXXF $DEFS "${INC[@]}" $2 -c "$1" -o "$OBJ/$n.o" & }
cc()  { n=$((n+1)); "$TC/avr-gcc" $CF $DEFS "${INC[@]}" -c "$1" -o "$OBJ/$n.o" & }

for f in Print.cpp Stream.cpp WString.cpp abi.cpp new.cpp; do cxx "$FW/cores/arduino/$f"; done
cxx "$FW/libraries/Wire/src/Wire.cpp"; cc "$FW/libraries/Wire/src/utility/twi.c"
cxx "$LD/RTClib/src/RTClib.cpp"
# AvrUtil.cpp 의 util_soft_reset(jmp 0) 은 이름을 돌려 두고 fakes 의 것(시험으로 복귀)을 쓴다.
while IFS= read -r f; do
    case "$f" in */AvrUtil.cpp) cxx "$f" -Dutil_soft_reset=product_util_soft_reset ;; *) cxx "$f" ;; esac
done < <(find "$ROOT/lib/TraceQ_Arduino/src" -name '*.cpp')
cxx "$ROOT/src/main.cpp"
cxx "$HERE/fakes/fake_hw.cpp"; cxx "$HERE/fakes/fake_mfrc522.cpp"
wait
for t in "$@"; do
    name=$(basename "$t" .cpp)
    "$TC/avr-g++" $CXXF $DEFS "${INC[@]}" -c "$t" -o "$OUTD/$name.o"
    "$TC/avr-g++" -mmcu=atmega2560 -Os -g $LDF -Wl,--gc-sections -Wl,--defsym=__stack=0x7FFF \
        -o "$OUTD/$name.elf" "$OBJ"/*.o "$OUTD/$name.o"
done
