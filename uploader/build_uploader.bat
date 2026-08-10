@echo off
rem TraceQ ?åÏõ®???ÖÎ°ú??exe ÎπåÎìú (Í∞úÎ∞ú PC ?ÑÏö©)
rem 1) pio run ?ºÎ°ú firmware.hex Î•?Î®ºÏ? ÎπåÎìú????Í≤?rem 2) ??bat ?§Ìñâ -> uploader\dist\TraceQ_FW_Upload.exe ?ùÏÑ±
cd /d "%~dp0"

copy /y "..\.pio\build\megaatmega2560\firmware.hex" firmware.hex
if errorlevel 1 goto :fail

rem version.hpp ?êÏÑú Î≤ÑÏ†Ñ Î¨∏Ïûê???êÎèô Ï∂îÏ∂ú -> fw_version.txt (?úÍ∏∞ Í∞±Ïã† ?ÑÎùΩ Î∞©Ï?)
py gen_version.py
if errorlevel 1 goto :fail

py -m PyInstaller --onefile --console --clean --name TraceQ_FW_Upload ^
  --add-data "C:\Users\alu5\.platformio\packages\tool-avrdude\avrdude.exe;avrdude" ^
  --add-data "C:\Users\alu5\.platformio\packages\tool-avrdude\avrdude.conf;avrdude" ^
  --add-data "C:\Users\alu5\.platformio\packages\tool-avrdude\libusb0.dll;avrdude" ^
  --add-data "firmware.hex;." ^
  --add-data "fw_version.txt;." ^
  traceq_fw_upload.py
if errorlevel 1 goto :fail

echo.
echo [OK] dist\TraceQ_FW_Upload.exe
goto :eof

:fail
echo [FAIL] build failed
exit /b 1
