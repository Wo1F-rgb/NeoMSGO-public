@echo off
setlocal
set "UE_ROOT=%ProgramFiles%\Epic Games\UE_5.7"
if defined UE57_ROOT set "UE_ROOT=%UE57_ROOT%"
if not exist "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" (
  echo Unreal Engine 5.7 was not found. Set UE57_ROOT to its installation folder.
  pause
  exit /b 1
)
call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" NeoMSGOEditor Win64 Development "-Project=%~dp0..\NeoMSGO.uproject" -WaitMutex -NoHotReloadFromIDE
set "BUILD_RESULT=%ERRORLEVEL%"
if not "%BUILD_RESULT%"=="0" (
  echo Build failed. Install Visual Studio C++ game development tools and Windows SDK.
) else (
  echo Build completed. Run Tools\Launch-PublicEditor.bat next.
)
pause
exit /b %BUILD_RESULT%
