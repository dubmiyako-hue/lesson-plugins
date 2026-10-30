@echo off
rem Run the Steinberg validator and pluginval on both plug-ins of a build.
rem Usage: tools\validate.cmd [Release|Debug] [pluginval strictness, default 5]
rem Logs: .ai-cache\validate\<plugin>-validator.txt and <plugin>-pluginval.txt. Exit code 1 when any run fails.
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
if /i "%CONFIG%"=="Release" (set "DIR=build-rel") else (set "DIR=build-dbg")
set "LEVEL=%~2"
if "%LEVEL%"=="" set "LEVEL=5"
set "VALIDATOR=D:\work\Claude\_sdk\vst3sdk-build\bin\validator.exe"
set "PLUGINVAL=D:\work\Claude\_sdk\pluginval\pluginval.exe"
set "LOGS=%ROOT%\.ai-cache\validate"
if not exist "%LOGS%" mkdir "%LOGS%"
set "FAILED="
call :one LessonEQ "Lesson EQ"
call :one LessonComp "Lesson Comp"
if defined FAILED (
  echo FAILED:%FAILED%
  exit /b 1
)
echo All validations passed.
exit /b 0

:one
set "BUNDLE=%ROOT%\%DIR%\plugin\%~1_artefacts\%CONFIG%\VST3\%~2.vst3"
if not exist "%BUNDLE%" (
  echo Not built: %BUNDLE% 1>&2
  set "FAILED=%FAILED% %~1-missing"
  exit /b 0
)
"%VALIDATOR%" -e "%BUNDLE%" > "%LOGS%\%~1-validator.txt" 2>&1
if errorlevel 1 (set "FAILED=%FAILED% %~1-validator") else (echo %~1: validator passed)
"%PLUGINVAL%" --strictness-level %LEVEL% --validate "%BUNDLE%" > "%LOGS%\%~1-pluginval.txt" 2>&1
if errorlevel 1 (set "FAILED=%FAILED% %~1-pluginval") else (echo %~1: pluginval passed, strictness %LEVEL%)
exit /b 0
