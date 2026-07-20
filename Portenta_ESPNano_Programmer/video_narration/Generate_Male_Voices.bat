@echo off
setlocal

if not defined PIPER_PYTHON set "PIPER_PYTHON=python"
set "PYTHON=%PIPER_PYTHON%"
set "GENERATOR=%~dp0Voice_en_US-libritts-high.py"
set "COMMON_ARGS=--speaker p4535 --prefix Male_ --noise-scale 0.3 --noise-w-scale 0.3"

"%PYTHON%" --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python could not be started:
    echo %PYTHON%
    exit /b 1
)

if not exist "%GENERATOR%" (
    echo ERROR: Narration generator was not found:
    echo %GENERATOR%
    exit /b 1
)

echo SIMULATION NARRATION: Generating six male voice files.
echo Speaker: Brett W. Downey ^(p4535^)
echo.

call :generate 0 "Voice 1 of 6"
if errorlevel 1 exit /b 1
call :generate 1 "Voice 2 of 6"
if errorlevel 1 exit /b 1
call :generate 2 "Voice 3 of 6"
if errorlevel 1 exit /b 1
call :generate 2a "Voice 4 of 6"
if errorlevel 1 exit /b 1
call :generate 3 "Voice 5 of 6"
if errorlevel 1 exit /b 1
call :generate 4 "Voice 6 of 6"
if errorlevel 1 exit /b 1

echo.
echo All six male narration files were generated.
exit /b 0

:generate
echo.
echo %~2 started...
"%PYTHON%" "%GENERATOR%" %COMMON_ARGS% --section %~1
if errorlevel 1 (
    echo ERROR: %~2 failed.
    exit /b 1
)
echo %~2 finished.
pause
exit /b 0
