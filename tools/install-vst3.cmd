@echo off
rem Copy the Release builds of Lesson EQ.vst3 and Lesson Comp.vst3 into the system VST3 folder.
rem Usage: tools\install-vst3.cmd [--dry-run]
rem The VST3 folder is under Program Files, so the script asks for administrator rights. A plug-in that a host
rem has loaded cannot be overwritten, so it stops while a DAW or any process that has loaded either plug-in runs.
rem --dry-run runs the checks without administrator rights and copies nothing.
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "SRC_EQ=%ROOT%\build-rel\plugin\LessonEQ_artefacts\Release\VST3\Lesson EQ.vst3"
set "SRC_COMP=%ROOT%\build-rel\plugin\LessonComp_artefacts\Release\VST3\Lesson Comp.vst3"
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

if not exist "%SRC_EQ%\%BIN_EQ%" goto nobuild
if not exist "%SRC_COMP%\%BIN_COMP%" goto nobuild
goto elevate

:nobuild
echo Build not found under %ROOT%\build-rel\plugin 1>&2
echo Build it first: tools\build.cmd Release 1>&2
goto fail

:elevate
if defined DRY goto check
fltmc >nul 2>&1 && goto check
if defined ELEVATED goto noadmin
echo Asking for administrator rights...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -ArgumentList '--elevated' -Verb RunAs"
if errorlevel 1 goto noadmin
echo The install continues in the administrator window.
exit /b 0

:noadmin
echo Administrator rights were not granted; nothing was copied. 1>&2
goto fail

:check
rem Known DAWs and test hosts by process name, plus any process that has loaded a module named like either plug-in
rem (tasklist /m). The CSV lines start with a quote; the "no tasks" line is localized and does not.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$hosts = 'Studio Pro*', 'Studio One', 'Ableton Live*', 'reaper', 'FL', 'FL64', 'Cubase*', 'Nuendo*', 'Bitwig*', 'Reason', 'Cakewalk', 'Waveform*', 'Renoise', 'EditorHost', 'pluginval', 'validator'; $found = @{}; Get-Process -Name $hosts -ErrorAction SilentlyContinue | ForEach-Object { $found[$_.Id] = $_.ProcessName }; foreach ($module in 'Lesson EQ.vst3', 'Lesson Comp.vst3') { tasklist /m $module /fo csv /nh | Where-Object { $_.StartsWith([char]34) } | ConvertFrom-Csv -Header Name, Id, Modules | ForEach-Object { $found[[int]$_.Id] = $_.Name } }; foreach ($id in $found.Keys) { '  {0} (PID {1})' -f $found[$id], $id }; if ($found.Count -gt 0) { exit 3 }; exit 0"
set "RC=%errorlevel%"
if "%RC%"=="0" goto copy
if not "%RC%"=="3" goto checkfail
echo Close the programs above first: a loaded plug-in cannot be overwritten. 1>&2
if defined DRY goto fail
choice /c RQ /m "R: check again, Q: quit"
if errorlevel 2 goto fail
goto check

:checkfail
echo The host check failed (PowerShell exit code %RC%); nothing was copied. 1>&2
goto fail

:copy
if not defined DRY goto docopy
echo No host is running. Would copy:
echo   %SRC_EQ%
echo   to %DST_EQ%
echo   %SRC_COMP%
echo   to %DST_COMP%
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
goto done

:copyfail
echo The copy failed; the plug-ins in %COMMON%\VST3 may be incomplete. 1>&2
goto fail

:done
set "RC=0"
goto end

:fail
set "RC=1"

:end
if defined ELEVATED pause
exit /b %RC%
