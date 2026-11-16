@echo off
rem preflate-wrap.cmd — adapt preflate's split/recombine CLI to the one-file-in,
rem one-file-out shape a heavenzip step needs.
rem
rem   preflate-wrap.cmd c IN OUT     split IN into OUT
rem   preflate-wrap.cmd d IN OUT     recombine OUT from IN
rem
rem preflate does not compress: it splits a deflate stream into the uncompressed data
rem (FILE.u) and the reconstruction information (FILE.r), and recombines the two back
rem into the original bytes. Two files in, two files out, so the pair is carried in a
rem tar — which Windows ships as tar.exe — and the step sees one file each way.
rem
rem preflate.exe is expected beside this script.
setlocal enabledelayedexpansion

set "MODE=%~1"
set "IN=%~2"
set "OUT=%~3"
set "PREFLATE=%~dp0preflate.exe"

if "%MODE%"=="" goto usage
if "%IN%"=="" goto usage
if "%OUT%"=="" goto usage
if not exist "%PREFLATE%" (
  echo preflate-wrap: %PREFLATE% is missing 1>&2
  exit /b 1
)

rem Absolute paths: the work happens in a temporary directory, and a relative OUT
rem would otherwise be created inside it and deleted with it.
for %%F in ("%IN%") do set "IN=%%~fF"
for %%F in ("%OUT%") do set "OUT=%%~fF"

set "WORK=%TEMP%\preflate-wrap-%RANDOM%%RANDOM%"
mkdir "%WORK%" 2>nul || (echo preflate-wrap: cannot create %WORK% 1>&2 & exit /b 1)

if /i "%MODE%"=="c" goto compress
if /i "%MODE%"=="d" goto decompress
goto usage

:compress
copy /y "%IN%" "%WORK%\payload" >nul || goto fail
"%PREFLATE%" -s "%WORK%\payload" >nul || (
  echo preflate-wrap: split failed 1>&2 & goto fail
)
if not exist "%WORK%\payload.u" (echo preflate-wrap: no .u produced 1>&2 & goto fail)
if not exist "%WORK%\payload.r" (echo preflate-wrap: no .r produced 1>&2 & goto fail)
tar -cf "%OUT%" -C "%WORK%" payload.u payload.r || goto fail
goto done

:decompress
tar -xf "%IN%" -C "%WORK%" || (echo preflate-wrap: cannot unpack %IN% 1>&2 & goto fail)
if not exist "%WORK%\payload.u" (echo preflate-wrap: archive has no payload.u 1>&2 & goto fail)
"%PREFLATE%" -r "%WORK%\payload" >nul || (
  echo preflate-wrap: recombine failed 1>&2 & goto fail
)
if not exist "%WORK%\payload" (echo preflate-wrap: recombine produced nothing 1>&2 & goto fail)
copy /y "%WORK%\payload" "%OUT%" >nul || goto fail
goto done

:done
rmdir /s /q "%WORK%" 2>nul
exit /b 0

:fail
rmdir /s /q "%WORK%" 2>nul
exit /b 1

:usage
echo usage: preflate-wrap.cmd (c^|d) IN OUT 1>&2
exit /b 2
