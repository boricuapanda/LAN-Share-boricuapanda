@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-windows-app.ps1" %*
exit /b %ERRORLEVEL%
