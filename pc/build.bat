@echo off
rem Mixtape: one-click build. Needs: Visual Studio 2022 (C++ desktop workload), CMake, Git.
setlocal
cd /d "%~dp0"
where cmake >nul 2>nul
if errorlevel 1 (
  set "VSCM=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
  if exist "%VSCM%\cmake.exe" (set "PATH=%VSCM%;%PATH%") else (
    echo CMake not found. Install CMake from cmake.org or Visual Studio with the C++ desktop workload.
    pause & exit /b 1))
where git >nul 2>nul
if errorlevel 1 (echo Git not found. Install it from git-scm.com & pause & exit /b 1)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (echo CONFIGURE FAILED - see the text above & pause & exit /b 1)
cmake --build build --config Release 2>&1 | powershell -NoProfile -Command "$input | Tee-Object -FilePath build-log.txt"
if not exist build\Release\Mixtape.exe (echo BUILD FAILED - see build-log.txt & pause & exit /b 1)
copy /y build\Release\Mixtape.exe Mixtape.exe >nul
echo.
echo DONE: Mixtape.exe is in this folder.
pause
