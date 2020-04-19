@echo off
setlocal

set bld=target

if not "%1"=="" goto %1

:release
set cflags=/O2
goto build

:debug
set cflags=/Zi /DDEBUG
goto build

:build
pushd "%~dp0"

if not exist env.bat goto init_env
call env.bat

set ldflags=/OPT:REF,ICF /NOLOGO /SUBSYSTEM:CONSOLE /NODEFAULTLIB /MACHINE:X64

if not exist %bld% (mkdir %bld%) else (del /f /q %bld%\*.*)

cd %bld%
cl /nologo /GS- %cflags%  /Fe:kraken.exe ..\src\main.c /link %ldflags% kernel32.lib user32.lib shell32.lib setupapi.lib
goto end

:init_env
rem Create starter env.bat
(
echo setlocal
echo set sdkdir=C:\Program Files ^(x86^)\Windows Kits\10
echo set sdkver=10.0.18362.0
echo set sdkinc=%%sdkdir%%\Include\%%sdkver%%
echo set vcdir=C:\Program Files ^(x86^)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.25.28610
echo:
echo endlocal^& ^(
echo     set "include=%%sdkinc%%\ucrt;%%sdkinc%%\shared;%%sdkinc%%\um;%%vcdir%%\include"
echo     set "lib=%%sdkdir%%\Lib\%%sdkver%%\um\x64"
echo     set "path=%%vcdir%%\bin\Hostx64\x64;%%path%%"
echo ^)
) > env.bat

echo New env.bat was created. Please edit variables in env.bat and run build.bat again.

:end
popd
