@echo off
setlocal EnableExtensions
REM ---------------------------------------------------------------------------
REM run_nastran95.bat - run a deck through nastran95.exe, which lives next to
REM this file.
REM
REM The exe is the whole solver: nothing to install, no other files, no
REM environment to set. Copy nastran95.exe anywhere and `nastran95 deck.dat`
REM works. This wrapper is only for convenience:
REM
REM   * drag a deck onto this file in Explorer
REM         -> deck.out and deck.log are written next to the deck, and the
REM            window stays open to show how it ended
REM   * run_nastran95 deck.dat [output_dir]
REM         -> the same from a command prompt, no pause
REM
REM Exit code: 0 ran to END OF JOB with no fatal message, 1 bad arguments or
REM a missing file, 2 no END OF JOB banner, 3 a USER or SYSTEM FATAL MESSAGE.
REM
REM Do not rename this file nastran95.bat. cmd.exe resolves a bare "nastran95"
REM to nastran95.exe before nastran95.bat (PATHEXT order), so a .bat of that
REM name is never reached and only confuses.
REM ---------------------------------------------------------------------------

set "EXE=%~dp0nastran95.exe"
if not exist "%EXE%" (
    echo run_nastran95: nastran95.exe is not next to this file in %~dp0
    exit /b 1
)

REM double-clicked or dragged onto: Explorer starts cmd.exe with /c, and the
REM window would close before anyone could read the result. Typed at a
REM prompt, CMDCMDLINE is just the shell's own path
set "PAUSE_AT_END=0"
echo %CMDCMDLINE% | find /i "/c" >nul && set "PAUSE_AT_END=1"

if "%~1"=="" (
    "%EXE%" --help
    echo.
    echo Drag a deck onto this file, or:  run_nastran95 deck.dat [output_dir]
    if "%PAUSE_AT_END%"=="1" pause
    exit /b 1
)

"%EXE%" %*
set "RC=%ERRORLEVEL%"
if "%PAUSE_AT_END%"=="1" (
    echo.
    echo exit code %RC%
    pause
)
exit /b %RC%
