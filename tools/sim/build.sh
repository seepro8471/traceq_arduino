#!/bin/bash
# 사용: build.sh <펌웨어 루트> <출력 폴더> <시험.cpp>...
# 제품 소스(src/main.cpp + lib/TraceQ_Arduino/src 전부)를 실물 헤더·실물 빌드 플래그로 1회 컴파일하고,
# 하드웨어에 닿는 함수만 fakes/ 의 가짜 정의로 링크한다. 시험마다 <출력 폴더>/<이름>.elf.
set -e
ROOT="$1"; OUTD="$2"; shift 2
# 요청된 시험의 옛 결과를 **먼저** 지운다 — 빌드가 실패해도 옛 elf 가 남으면 run.sh 가 옛 결과로 초록을 낸다.
for t in "$@"; do name=$(basename "$t" .cpp); rm -f "$OUTD/$name.elf" "$OUTD/$name.elf.data.bin" "$OUTD/$name.o"; done
HERE="$(cd "$(dirname "$0")" && pwd)"
TC="/c/Users/alu5/.platformio/packages/toolchain-atmelavr/bin"
FW="/c/Users/alu5/.platformio/packages/framework-arduino-avr"
LD="/d/traceq_arduino 2.0/.pio/libdeps/megaatmega2560"
OBJ="$OUTD/obj"; rm -rf "$OBJ"; mkdir -p "$OBJ"

DEFS="-DARDUINO=10808 -DARDUINO_AVR_MEGA2560 -DARDUINO_ARCH_AVR -DF_CPU=16000000L
      -DSERIAL_RX_BUFFER_SIZE=512 -DTRACEQ_RFID_VERIFY_WRITES=1 -DTRACEQ_RFID_MAX_WRITE_RETRIES=3
      -DMFRC522_SPICLOCK=1000000UL ${HH2_DEFS}"
INC=(-I"$HERE/fakes" -I"$HERE/tests" -I"$FW/cores/arduino" -I"$FW/variants/mega"
     -I"$FW/libraries/EEPROM/src" -I"$FW/libraries/SPI/src" -I"$FW/libraries/Wire/src"
     -I"$LD/ArduinoJson/src" -I"$LD/RTClib/src" -I"$LD/Adafruit BusIO" -I"$LD/LiquidCrystal_I2C"
     -I"$ROOT/lib/MFRC522" -I"$ROOT/lib/TraceQ_Arduino/src")
# 시뮬 데이터공간은 0x200~0xFFFF — 실칩 8KB 와 무관한 **시험 장치만의 값**이다(제품 스택은 따로 잰다).
STACK_TOP=0x7FFF
RAM_LIMIT=28000
CXXF="-mmcu=atmega2560 -Os -g -std=gnu++11 -fno-exceptions -fno-threadsafe-statics -DSIM_STACK_TOP=$STACK_TOP
      -ffunction-sections -fdata-sections -w"
LDF=""
# 제품과 같은 LTO(PlatformIO atmelavr 기본). LTO=0 이면 끈다.
if [ "${LTO:-1}" = "1" ]; then CXXF="$CXXF -flto -fno-fat-lto-objects"; LDF="-flto -fuse-linker-plugin"; fi
CF="-mmcu=atmega2560 -Os -g -ffunction-sections -fdata-sections -w"

n=0; pids=()
cxx() { n=$((n+1)); "$TC/avr-g++" $CXXF $DEFS "${INC[@]}" $2 -c "$1" -o "$OBJ/$n.o" & pids+=($!); }
cc()  { n=$((n+1)); "$TC/avr-gcc" $CF $DEFS "${INC[@]}" -c "$1" -o "$OBJ/$n.o" & pids+=($!); }

for f in Print.cpp Stream.cpp WString.cpp abi.cpp new.cpp; do cxx "$FW/cores/arduino/$f"; done
cxx "$FW/libraries/Wire/src/Wire.cpp"; cc "$FW/libraries/Wire/src/utility/twi.c"
cxx "$LD/RTClib/src/RTClib.cpp"
# AvrUtil.cpp 의 util_soft_reset(jmp 0) 은 이름을 돌려 두고 fakes 의 것(시험으로 복귀)을 쓴다.
while IFS= read -r f; do
    case "$f" in */AvrUtil.cpp) cxx "$f" -Dutil_soft_reset=product_util_soft_reset ;; *) cxx "$f" ;; esac
done < <(find "$ROOT/lib/TraceQ_Arduino/src" -name '*.cpp')
cxx "$ROOT/src/main.cpp"
cxx "$HERE/fakes/fake_hw.cpp"; cxx "$HERE/fakes/fake_mfrc522.cpp"
# 인자 없는 `wait` 는 배경 컴파일이 실패해도 0 이다 — 하나씩 기다려 실패면 elf 를 만들지 않고 끝낸다.
bg_fail=0
for p in "${pids[@]}"; do wait "$p" || bg_fail=1; done
if [ $bg_fail -ne 0 ]; then echo "★배경 컴파일 실패 — 시험 elf 를 만들지 않는다." >&2; exit 1; fi
for t in "$@"; do
    name=$(basename "$t" .cpp)
    "$TC/avr-g++" $CXXF $DEFS "${INC[@]}" -c "$t" -o "$OUTD/$name.o"
    "$TC/avr-g++" -mmcu=atmega2560 -Os -g $LDF -Wl,--gc-sections -Wl,--defsym=__stack=$STACK_TOP \
        -o "$OUTD/$name.elf" "$OBJ"/*.o "$OUTD/$name.o"
    # 시뮬 RAM 은 0x200~$STACK_TOP. 정적 영역이 커지면 스택과 겹쳐 **시험이 조용히 틀린 결과**를 낸다
    # (2026-09-23: SimCard 16장으로 .bss 34KB → 뒤쪽 시험 4개가 거짓 빨강, 멈추지도 않았다).
    ram=$("$TC/avr-size" -A "$OUTD/$name.elf" | awk '/^\.data|^\.bss/ {s+=$2} END {print s+0}')
    if [ "$ram" -gt "$RAM_LIMIT" ]; then
        echo "★$name: 정적 RAM ${ram}B > ${RAM_LIMIT}B — 스택과 겹친다. SimCard 를 줄이고 돌려쓸 것." >&2
        rm -f "$OUTD/$name.elf"      # 남겨 두면 runall 이 그대로 돌려 초록으로 보인다
        RAM_OVER=1
    fi
done
if [ -n "$RAM_OVER" ]; then exit 1; fi
exit 0
