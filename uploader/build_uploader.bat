@echo off
rem TraceQ 펌웨어 업로더 exe 빌드 (개발 PC 전용)
rem 1) pio run 으로 firmware.hex 를 먼저 빌드해 둘 것
rem 2) 이 bat 실행 -> uploader\dist\TraceQ_FW_Upload.exe 생성
cd /d "%~dp0"

copy /y "..\.pio\build\megaatmega2560\firmware.hex" firmware.hex
if errorlevel 1 goto :fail

py -m PyInstaller --onefile --console --clean --name TraceQ_FW_Upload ^
  --add-data "C:\Users\alu5\.platformio\packages\tool-avrdude\avrdude.exe;avrdude" ^
  --add-data "C:\Users\alu5\.platformio\packages\tool-avrdude\avrdude.conf;avrdude" ^
  --add-data "C:\Users\alu5\.platformio\packages\tool-avrdude\libusb0.dll;avrdude" ^
  --add-data "firmware.hex;." ^
  traceq_fw_upload.py
if errorlevel 1 goto :fail

echo.
echo [OK] dist\TraceQ_FW_Upload.exe
goto :eof

:fail
echo [FAIL] build failed
exit /b 1
