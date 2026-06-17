@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-windows-installer.ps1" %*
exit /b %ERRORLEVEL%
