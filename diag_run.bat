@echo off
cd /d "%~dp0"
echo === Tubelight diag run %date% %time% > diag_out.log
echo cwd=%cd% >> diag_out.log
certutil -hashfile tubelight.exe SHA256 >> diag_out.log 2>&1
echo. >> diag_out.log
echo === launching tubelight.exe --overlay >> diag_out.log
tubelight.exe --overlay 1>>diag_out.log 2>>&1
echo === exit code: %errorlevel% >> diag_out.log
echo Log saved to %cd%\diag_out.log
echo Press any key to close...
pause >nul
