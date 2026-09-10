@echo off
setlocal
set "TEMP=%~dp0..\build-vcam-temp"
set "TMP=%TEMP%"
if not exist "%TEMP%" mkdir "%TEMP%"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (
  echo Visual Studio with Desktop development for C++ was not found.
  exit /b 2
)
set "TOOLSET=v143"
if exist "%VSROOT%\VC\Auxiliary\Build\Microsoft.VCToolsVersion.v145.default.txt" set "TOOLSET=v145"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
"%VSROOT%\MSBuild\Current\Bin\MSBuild.exe" "%~dp0..\vendor\windows-camera\VirtualCameraMediaSource\VirtualCameraMediaSource.vcxproj" /m /verbosity:minimal /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=%TOOLSET% /p:WindowsTargetPlatformVersion=10.0.26100.0
exit /b %errorlevel%
