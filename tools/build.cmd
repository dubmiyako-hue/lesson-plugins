@echo off
rem Build the page (npm), then configure and build with MSVC + NMake.
rem Usage: tools\build.cmd [Release|Debug]   (build dirs: build-rel, build-dbg)
setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
if /i "%CONFIG%"=="Release" (set "DIR=build-rel") else (set "DIR=build-dbg")
set "ROOT=%~dp0.."
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
rem The page first: the build embeds plugin/web/dist.
pushd "%ROOT%" || exit /b 1
call npm run web:build || (popd & exit /b 1)
popd
call "%VCVARS%" >nul || exit /b 1
cmake -S "%ROOT%" -B "%ROOT%\%DIR%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=%CONFIG% || exit /b 1
cmake --build "%ROOT%\%DIR%" || exit /b 1
