@echo off
setlocal
set "DIAGROOT=%~dp0..\build-diagnostics"
set "TEMP=%DIAGROOT%\temp"
set "TMP=%TEMP%"
if not exist "%TEMP%" mkdir "%TEMP%"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (
  echo Visual Studio with Desktop development for C++ was not found.
  exit /b 2
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++20 /EHsc /O2 /DUNICODE /D_UNICODE /DNOMINMAX ^
  /I"%~dp0..\src" ^
  "%~dp0..\src\vcamctl\main.cpp" ^
  "%~dp0..\src\app\camera_capture.cpp" ^
  "%~dp0..\src\app\camera_devices.cpp" ^
  "%~dp0..\src\core\diagnostics.cpp" ^
  "%~dp0..\src\shared\frame_transport.cpp" ^
  /Fo:"%DIAGROOT%\\" ^
  /Fe:"%DIAGROOT%\SubliminalCamVcamCtl.exe" ^
  /link mfplat.lib mf.lib mfreadwrite.lib mfuuid.lib mfsensorgroup.lib ole32.lib advapi32.lib
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++20 /EHsc /O2 /DUNICODE /D_UNICODE /DNOMINMAX ^
  /I"%~dp0..\src" ^
  "%~dp0..\tests\core_tests.cpp" ^
  "%~dp0..\src\core\compositor.cpp" ^
  "%~dp0..\src\core\settings.cpp" ^
  /Fo:"%DIAGROOT%\\" ^
  /Fe:"%DIAGROOT%\subliminalcam_tests.exe"
if errorlevel 1 exit /b %errorlevel%
"%DIAGROOT%\subliminalcam_tests.exe"
exit /b %errorlevel%
