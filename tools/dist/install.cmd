@echo off
rem Install Lesson EQ and Lesson Comp (VST3) from the folder this script is in into the system VST3 folder.
rem Usage: install.cmd [--dry-run]
rem Close your DAW first. The VST3 folder is under Program Files, so the script asks for administrator rights.
rem --dry-run shows the source and the destination and copies nothing. It needs no administrator rights.
setlocal
set "SRC_EQ=%~dp0Lesson EQ.vst3"
set "SRC_COMP=%~dp0Lesson Comp.vst3"
set "COMMON=%CommonProgramW6432%"
if not defined COMMON set "COMMON=%CommonProgramFiles%"
set "DST_EQ=%COMMON%\VST3\Lesson EQ.vst3"
set "DST_COMP=%COMMON%\VST3\Lesson Comp.vst3"
set "BIN_EQ=Contents\x86_64-win\Lesson EQ.vst3"
set "BIN_COMP=Contents\x86_64-win\Lesson Comp.vst3"
set "DRY="
set "ELEVATED="
if /i "%~1"=="--dry-run" set "DRY=1"
if /i "%~1"=="--elevated" set "ELEVATED=1"

if not exist "%SRC_EQ%\%BIN_EQ%" goto nosrc_eq
if not exist "%SRC_COMP%\%BIN_COMP%" goto nosrc_comp
goto admin

:nosrc_eq
echo Not found: "%SRC_EQ%\%BIN_EQ%" 1>&2
goto nosrc
:nosrc_comp
echo Not found: "%SRC_COMP%\%BIN_COMP%" 1>&2
:nosrc
echo Run install.cmd from the unzipped folder that holds both Lesson .vst3 folders. 1>&2
goto fail

:admin
if defined DRY goto dryrun
fltmc >nul 2>&1 && goto docopy
if defined ELEVATED goto noadmin
echo Asking for administrator rights...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -ArgumentList '--elevated' -Verb RunAs"
if errorlevel 1 goto noadmin
echo The install continues in the administrator window.
exit /b 0

:noadmin
echo Administrator rights were not granted; nothing was copied. 1>&2
goto fail

:dryrun
echo Dry run: nothing is copied. The install would copy:
echo   from %SRC_EQ%
echo   to   %DST_EQ%
echo   from %SRC_COMP%
echo   to   %DST_COMP%
goto done

:docopy
rem /R:2 /W:2: robocopy otherwise retries a locked file a million times, 30 s apart.
robocopy "%SRC_EQ%" "%DST_EQ%" /MIR /R:2 /W:2 /NJH /NJS /NDL /NP
if errorlevel 8 goto copyfail
fc /b "%SRC_EQ%\%BIN_EQ%" "%DST_EQ%\%BIN_EQ%" >nul || goto copyfail
robocopy "%SRC_COMP%" "%DST_COMP%" /MIR /R:2 /W:2 /NJH /NJS /NDL /NP
if errorlevel 8 goto copyfail
fc /b "%SRC_COMP%\%BIN_COMP%" "%DST_COMP%\%BIN_COMP%" >nul || goto copyfail
echo Installed: %DST_EQ%
echo Installed: %DST_COMP%
echo Rescan the plug-ins in your DAW.
goto done

:copyfail
echo The copy failed or could not be verified. If your DAW or another VST3 host is running, close it and run install.cmd again. 1>&2
echo Until then, the files in %COMMON%\VST3 may be incomplete. 1>&2
goto fail

:done
set "RC=0"
goto end

:fail
set "RC=1"

:end
if defined ELEVATED pause
exit /b %RC%
