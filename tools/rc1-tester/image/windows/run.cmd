@echo off
REM Copyright 2026 Lusoris
REM SPDX-License-Identifier: EUPL-1.2
REM
REM Runs the VMAFx tester report from this unpacked folder and writes report.json into
REM the folder you run it from (a summary goes to the console). Reads this folder,
REM writes nothing outside it except report.json and your temporary folder. The report
REM program contains no network code. Works from PowerShell (.\run.cmd) and from the
REM Command Prompt (run.cmd); any options are passed on to the report program.
setlocal
set "VMAFX_HERE=%~dp0"
set "VMAFX_PACKAGE_ARCH="
set /p VMAFX_PACKAGE_ARCH=<"%VMAFX_HERE%image\package-arch.txt"
if /i not "%PROCESSOR_ARCHITECTURE%"=="%VMAFX_PACKAGE_ARCH%" (
  echo run.cmd: this zip is for %VMAFX_PACKAGE_ARCH% Windows, this machine is %PROCESSOR_ARCHITECTURE%: download the zip for your machine 1>&2
  exit /b 64
)
"%VMAFX_HERE%runtime\python.exe" -I -B -X utf8 "%VMAFX_HERE%tester\vmaf-tester-report" --image-root "%VMAFX_HERE%." --output report.json %*
exit /b %ERRORLEVEL%
