@echo off
rem Run a command inside the MSVC x64 developer environment.
rem Usage: tools\msvc.cmd <command> [args...]
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo vcvars64.bat not found: %VCVARS% 1>&2
  exit /b 1
)
call "%VCVARS%" >nul || exit /b 1
%*
