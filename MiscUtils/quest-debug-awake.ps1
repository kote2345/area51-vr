<#!
.SYNOPSIS
    Keeps a Meta Quest awake while it is being debugged over ADB.

.DESCRIPTION
    This is a developer-machine helper only. It does not change the game APK.
    The proximity override is session/boot scoped by Quest and normally has to
    be enabled again after a headset reboot.

.EXAMPLE
    .\quest-debug-awake.ps1 on

.EXAMPLE
    .\quest-debug-awake.ps1 off
#>

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('on', 'off', 'status')]
    [string] $Action = 'on'
)

$adbCandidates = @()
if ($env:LOCALAPPDATA) {
    $adbCandidates += (Join-Path $env:LOCALAPPDATA 'Android\Sdk\platform-tools\adb.exe')
}
if ($env:ANDROID_HOME) {
    $adbCandidates += (Join-Path $env:ANDROID_HOME 'platform-tools\adb.exe')
}
if ($env:ANDROID_SDK_ROOT) {
    $adbCandidates += (Join-Path $env:ANDROID_SDK_ROOT 'platform-tools\adb.exe')
}

$adb = $adbCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (-not $adb) {
    $adb = (Get-Command adb.exe -ErrorAction SilentlyContinue).Source
}
if (-not $adb) {
    throw 'adb.exe was not found. Install Android SDK Platform-Tools or add it to PATH.'
}

function Invoke-Adb {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]] $Arguments)
    & $adb @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "ADB завершился с кодом $LASTEXITCODE"
    }
}

$devices = @(& $adb devices | Select-String "\tdevice$")
if ($devices.Count -eq 0) {
    throw 'Quest is not connected or USB debugging was not authorized.'
}

switch ($Action) {
    'on' {
        # Quest VrPowerManager treats this as an on-face signal. It is not an
        # APK setting and is reset by a headset reboot.
        Invoke-Adb shell am broadcast -a com.oculus.vrpowermanager.prox_close
        Invoke-Adb shell input keyevent KEYCODE_WAKEUP
        Write-Host 'Quest: proximity sleep disabled for the current boot.'
    }
    'off' {
        Invoke-Adb shell am broadcast -a com.oculus.vrpowermanager.automation_disable
        Write-Host 'Quest: normal proximity sensor behavior restored.'
    }
    'status' {
        Invoke-Adb shell dumpsys power | Select-String 'mWakefulness|mScreenOn|mUserActivityTimeout'
    }
}
