@echo off
REM ============================================================
REM  Tubelight — CRT overlay WITH click-through (use the PC normally
REM  underneath). Launches the true fullscreen mode (NOT windowed —
REM  windowed has click-through OFF by design, and the in-app fullscreen
REM  TOGGLE from windowed does NOT enable click-through either).
REM
REM  Edit the profile/signal/renderer below to taste.
REM    --renderer dx12  = zero-copy capture (best for emulators)
REM    --renderer gl    = original OpenGL path
REM  Quit the overlay with  Ctrl+Alt+Q.
REM ============================================================

cd /d "%~dp0"

REM Kill any leftover/zombie instances first (they stack as topmost
REM windows and steal the clicks). Harmless if none are running.
taskkill /F /IM tubelight.exe >nul 2>&1

REM TUBELIGHT_CT_LOG=1 writes tubelight_clickthrough.log next to the exe
REM (handy if something still misbehaves — send me that file).
set TUBELIGHT_CT_LOG=1

start "" "%~dp0tubelight.exe" --overlay-fullscreen --renderer dx12 --profile pvm-8220 --signal composite_ntsc
