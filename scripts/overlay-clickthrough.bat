@echo off
cd /d "%~dp0"
taskkill /F /IM tubelight.exe >nul 2>&1
start "" "%~dp0tubelight.exe" --overlay-fullscreen --renderer dx12 --profile pvm-8220 --signal composite_ntsc
