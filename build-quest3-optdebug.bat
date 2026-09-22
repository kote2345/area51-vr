@echo off
setlocal

set "ROOT=%~dp0"
set "SDK_DIR=C:/Users/user/AppData/Local/Android/Sdk"
set "GRADLE=C:\Gradle\gradle-8.9\bin\gradle.bat"
set "ANDROID_DIR=%ROOT%Apps\GameApp\android"
set "LOCAL_PROPS=%ANDROID_DIR%\local.properties"
set "BACKUP_PROPS=%TEMP%\area51-local.properties.%RANDOM%.bak"
set "Path=C:\Program Files\Git\usr\bin;%Path%"

if exist "%LOCAL_PROPS%" copy /y "%LOCAL_PROPS%" "%BACKUP_PROPS%" >nul
set "ANDROID_HOME=%SDK_DIR%"
set "ANDROID_SDK_ROOT=%SDK_DIR%"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$p=$env:LOCAL_PROPS; $s=Get-Content -LiteralPath $p -Raw; if($s -match '(?m)^sdk\.dir=.*$'){ $s=[regex]::Replace($s,'(?m)^sdk\.dir=.*$','sdk.dir='+$env:SDK_DIR) } else { $s=$s.TrimEnd()+[Environment]::NewLine+'sdk.dir='+$env:SDK_DIR+[Environment]::NewLine }; Set-Content -LiteralPath $p -Value $s -NoNewline"
if errorlevel 1 goto :restore

pushd "%ANDROID_DIR%"
call "%GRADLE%" :app:assembleDebug --no-daemon -PA51_ENABLE_OPENXR=true -PA51_ANDROID_CONFIGURATION=OptDebug
set "BUILD_EXIT=%ERRORLEVEL%"
popd

:restore
if exist "%BACKUP_PROPS%" (
    copy /y "%BACKUP_PROPS%" "%LOCAL_PROPS%" >nul
    del /q "%BACKUP_PROPS%" >nul 2>&1
)

if not defined BUILD_EXIT set "BUILD_EXIT=1"
if "%BUILD_EXIT%"=="0" (
    echo.
    echo Optimized profiling APK:
    echo %ANDROID_DIR%\app\build\outputs\apk\debug\app-debug.apk
)
exit /b %BUILD_EXIT%
