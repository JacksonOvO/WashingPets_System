@echo off
setlocal

set "BUNDLE_ROOT=%LOCALAPPDATA%\stm32cube\bundles"

for /f "delims=" %%D in ('dir /b /ad /o-n "%BUNDLE_ROOT%\cmake" 2^>nul') do if not defined CMAKE_VERSION set "CMAKE_VERSION=%%D"
for /f "delims=" %%D in ('dir /b /ad /o-n "%BUNDLE_ROOT%\ninja" 2^>nul') do if not defined NINJA_VERSION set "NINJA_VERSION=%%D"
for /f "delims=" %%D in ('dir /b /ad /o-n "%BUNDLE_ROOT%\gnu-tools-for-stm32" 2^>nul') do if not defined GCC_VERSION set "GCC_VERSION=%%D"

if not defined CMAKE_VERSION (
  echo STM32Cube CMake bundle was not found under "%BUNDLE_ROOT%". 1>&2
  exit /b 1
)
if not defined NINJA_VERSION (
  echo STM32Cube Ninja bundle was not found under "%BUNDLE_ROOT%". 1>&2
  exit /b 1
)
if not defined GCC_VERSION (
  echo STM32Cube ARM GCC bundle was not found under "%BUNDLE_ROOT%". 1>&2
  exit /b 1
)

set "CMAKE_EXE=%BUNDLE_ROOT%\cmake\%CMAKE_VERSION%\bin\cmake.exe"
set "PATH=%BUNDLE_ROOT%\ninja\%NINJA_VERSION%\bin;%BUNDLE_ROOT%\gnu-tools-for-stm32\%GCC_VERSION%\bin;%PATH%"

if not exist "%CMAKE_EXE%" (
  echo CMake executable was not found: "%CMAKE_EXE%". 1>&2
  exit /b 1
)

"%CMAKE_EXE%" %*
exit /b %errorlevel%
