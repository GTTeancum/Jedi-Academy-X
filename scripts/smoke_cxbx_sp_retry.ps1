param(
    [string]$Repo = "C:\Programming\GitHub\Jedi-Academy-X",
    [string]$Cxbx = "C:\Programming\GitHub\Jedi-Academy-X\CXBXR",
    [string]$Game = "C:\Games\Emulators\CXBX\Jedi Academy rebuild",
    [string]$Junction = "C:\Games\Emulators\CXBX\Jedi Academy rebuild",
    [string]$LoaderName = "cxbxr-ldr-project2.exe",
    [string]$Level = "",
    [string[]]$StartupCommand = @(),
    [int]$WatchdogSeconds = 300,
    [int]$ActiveSeconds = 0,
    [int]$InitialQuietGraceSeconds = 180,
    [int]$QuietGraceSeconds = 20,
    [int]$MaxAttempts = 2,
    [int]$StallRetryDelaySeconds = 180,
    [int]$EarlyLogRetryBytes = 50000,
    [switch]$AllowNoActive,
    [switch]$NoCopy
)

$ErrorActionPreference = "Stop"

function Get-CxbxProjectProcesses {
    Get-CimInstance Win32_Process |
        Where-Object {
            $_.Name -in @("cxbx-project2.exe", "cxbxr-ldr-project2.exe")
        }
}

function Stop-CxbxProjectProcesses {
    Get-CxbxProjectProcesses | ForEach-Object {
        try {
            Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
        } catch {
        }
    }
}

function Get-StatusFromOutput([string[]]$Output) {
    return Get-ValueFromOutput $Output "status"
}

function Get-ValueFromOutput([string[]]$Output, [string]$Key) {
    foreach ($line in $Output) {
        if ($line -match "^$([regex]::Escape($Key))=(.*)$") {
            return $matches[1]
        }
    }
    return ""
}

function Test-RetryableSmokeStall([string[]]$Output, [string]$Status, [int]$ExitCode) {
    if ($ExitCode -eq 0) {
        return $false
    }

    if ($Status -eq "FAIL_LOG_STALLED_PROCESS_ALIVE" -or $Status -eq "FAIL_FRAME_HEARTBEAT_STALLED") {
        return $true
    }

    if ($Status -ne "FAIL_NOT_ACTIVE") {
        return $false
    }

    $fatalCountText = Get-ValueFromOutput $Output "fatalCount"
    $logPath = Get-ValueFromOutput $Output "logPath"
    $fatalCount = 0
    [void][int]::TryParse($fatalCountText, [ref]$fatalCount)

    if ($fatalCount -gt 0 -or !(Test-Path $logPath)) {
        return $false
    }

    return ((Get-Item $logPath).Length -lt $EarlyLogRetryBytes)
}

$smokeScript = Join-Path $Repo "scripts\smoke_cxbx_sp.ps1"
$attempt = 1

while ($attempt -le $MaxAttempts) {
    "attempt=$attempt"

    $args = @(
        "-ExecutionPolicy", "Bypass",
        "-File", $smokeScript,
        "-Repo", $Repo,
        "-Cxbx", $Cxbx,
        "-Game", $Game,
        "-Junction", $Junction,
        "-LoaderName", $LoaderName,
        "-WatchdogSeconds", $WatchdogSeconds,
        "-ActiveSeconds", $ActiveSeconds,
        "-InitialQuietGraceSeconds", $InitialQuietGraceSeconds,
        "-QuietGraceSeconds", $QuietGraceSeconds
    )

    if ($AllowNoActive) {
        $args += "-AllowNoActive"
    }
    if ($Level) {
        $args += @("-Level", $Level)
    }
    if ($StartupCommand -and $StartupCommand.Count -gt 0) {
        foreach ($command in $StartupCommand) {
            $args += @("-StartupCommand", $command)
        }
    }
    if ($NoCopy) {
        $args += "-NoCopy"
    }

    $output = & powershell @args
    $exitCode = $LASTEXITCODE
    $output

    $status = Get-StatusFromOutput $output
    if ($exitCode -eq 0) {
        exit 0
    }

    $isStall = Test-RetryableSmokeStall $output $status $exitCode
    if (!$isStall -or $attempt -ge $MaxAttempts) {
        exit $exitCode
    }

    "stallRetry=1"
    "stallRetryDelaySeconds=$StallRetryDelaySeconds"
    Stop-CxbxProjectProcesses
    Start-Sleep -Seconds $StallRetryDelaySeconds

    $attempt++
}

exit 1
