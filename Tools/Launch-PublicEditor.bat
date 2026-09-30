@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Launch-PublicEditor.ps1" %*
exit /b %ERRORLEVEL%
