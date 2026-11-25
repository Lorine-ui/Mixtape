@echo off
rem Mixtape для Android: сборка APK одним щелчком.
rem Нужны JDK 17, Android SDK (platform 34, build-tools 34, NDK 26.3, CMake 3.22) и Gradle 8.7 в %USERPROFILE%\Android.
setlocal
set "AND=%USERPROFILE%\Android"
set "JAVA_HOME=%AND%\jdk-17"
set "ANDROID_HOME=%AND%\Sdk"
set "PATH=%JAVA_HOME%\bin;%PATH%"
if not exist "%JAVA_HOME%\bin\java.exe" (echo JDK 17 not found in %JAVA_HOME% & pause & exit /b 1)
if not exist "%ANDROID_HOME%\ndk" (echo Android SDK/NDK not found in %ANDROID_HOME% & pause & exit /b 1)

rem Gradle и CMake не любят русские буквы в пути: собираем через ссылку с латинским именем
set "LINK=%AND%\mixtape-src"
if exist "%LINK%" rmdir "%LINK%"
mklink /J "%LINK%" "%~dp0.." >nul
cd /d "%LINK%\android"

> local.properties echo sdk.dir=%ANDROID_HOME:\=\\%
call "%AND%\gradle-8.7\bin\gradle.bat" --no-daemon assembleRelease
if errorlevel 1 (echo BUILD FAILED & pause & exit /b 1)
copy /y "app\build\outputs\apk\release\app-release.apk" "%~dp0Mixtape.apk" >nul
echo.
echo DONE: android\Mixtape.apk
pause
