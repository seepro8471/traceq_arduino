@echo off
setlocal EnableExtensions
rem ===================================================================
rem  TraceQ 펌웨어 업로더 exe 빌드 (개발 PC 전용)
rem
rem  1) 먼저 펌웨어를 빌드해 둘 것:  py -m platformio run
rem  2) 이 파일 실행 -> dist\TraceQ_FW_Upload_<버전>.exe 생성
rem
rem  *파일명에 펌웨어 버전이 붙는다(예: TraceQ_FW_Upload_2.2.6.exe).
rem   버전은 version.hpp 에서 자동 추출하므로 여기서 손댈 것이 없다.
rem  *이 파일은 cp949(ANSI)로 저장한다 - UTF-8 로 저장하면 cmd 가 한글
rem   주석을 오파싱해 빌드가 깨진다.
rem ===================================================================
cd /d "%~dp0"

copy /y "..\.pio\build\megaatmega2560\firmware.hex" firmware.hex
if errorlevel 1 goto :fail

rem version.hpp 에서 버전 문자열 자동 추출 -> fw_version.txt (표기 갱신 누락 방지)
py gen_version.py
if errorlevel 1 goto :fail

rem 추출한 버전을 파일명에 사용
set /p FWVER=<fw_version.txt
if "%FWVER%"=="" goto :fail
set "OUTNAME=TraceQ_FW_Upload_%FWVER%"

py -m PyInstaller --onefile --console --clean --name %OUTNAME% ^
  --add-data "C:\Users\alu5\.platformio\packages\tool-avrdude\avrdude.exe;avrdude" ^
  --add-data "C:\Users\alu5\.platformio\packages\tool-avrdude\avrdude.conf;avrdude" ^
  --add-data "C:\Users\alu5\.platformio\packages\tool-avrdude\libusb0.dll;avrdude" ^
  --add-data "firmware.hex;." ^
  --add-data "fw_version.txt;." ^
  traceq_fw_upload.py
if errorlevel 1 goto :fail

echo.
echo [OK] dist\%OUTNAME%.exe
echo      (Defender 예외 bat 은 TraceQ_FW_Upload*.exe 로 등록하므로
echo       버전이 바뀌어도 다시 실행할 필요가 없다)
goto :eof

:fail
echo [FAIL] build failed
exit /b 1
