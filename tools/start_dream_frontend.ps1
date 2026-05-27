param(
  [string]$Source = "COM10",
  [string]$Target = "COM6",
  [int]$SourceBaud = 57600,
  [int]$TargetBaud = 115200,
  [double]$SendRate = 20.0,
  [string]$WebHost = "127.0.0.1",
  [int]$WebPort = 8765
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$python = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\python.exe"
$bridgeScript = Join-Path $repoRoot "tools\dream_eeg_serial_bridge.py"
$logDir = Join-Path $repoRoot "logs"
$outLog = Join-Path $logDir "dream_bridge.out.log"
$errLog = Join-Path $logDir "dream_bridge.err.log"

if (!(Test-Path $python)) {
  throw "PlatformIO Python not found: $python"
}

if (!(Test-Path $bridgeScript)) {
  throw "Bridge script not found: $bridgeScript"
}

New-Item -ItemType Directory -Force $logDir | Out-Null

$existing = Get-NetTCPConnection -LocalAddress $WebHost -LocalPort $WebPort -State Listen -ErrorAction SilentlyContinue
if ($existing) {
  Write-Host "Dream frontend already appears to be listening at http://${WebHost}:${WebPort}/"
  Write-Host "PID: $($existing.OwningProcess)"
  exit 0
}

$arguments = @(
  $bridgeScript,
  "--source", $Source,
  "--target", $Target,
  "--source-baud", $SourceBaud,
  "--target-baud", $TargetBaud,
  "--send-rate", $SendRate,
  "--web-host", $WebHost,
  "--web-port", $WebPort
)

Write-Host "Starting Dream frontend..."
Write-Host "Source EEG serial: $Source@$SourceBaud"
Write-Host "Target M5 serial:  $Target@$TargetBaud"
Write-Host "Web URL:           http://${WebHost}:${WebPort}/"
Write-Host "Logs:              $outLog"

$process = Start-Process `
  -FilePath $python `
  -ArgumentList $arguments `
  -WorkingDirectory $repoRoot `
  -RedirectStandardOutput $outLog `
  -RedirectStandardError $errLog `
  -WindowStyle Hidden `
  -PassThru

Start-Sleep -Seconds 2

if ($process.HasExited) {
  Write-Host "Dream frontend exited during startup."
  if (Test-Path $errLog) {
    Write-Host ""
    Write-Host "Error log:"
    Get-Content -Tail 80 $errLog
  }
  exit 1
}

$stateUrl = "http://${WebHost}:${WebPort}/api/state"
try {
  $state = Invoke-RestMethod -Uri $stateUrl -TimeoutSec 3
  Write-Host "Dream frontend is running."
  Write-Host "PID:               $($process.Id)"
  Write-Host "Open:              http://${WebHost}:${WebPort}/"
  Write-Host "Source open:       $($state.bridge.sourceOpen)"
  Write-Host "Target open:       $($state.bridge.targetOpen)"
  Write-Host "M5 seen:           $($state.m5.seen)"
  Write-Host "Microduino seen:   $($state.mic.seen)"
} catch {
  Write-Host "Process started, but health check did not respond yet."
  Write-Host "PID:               $($process.Id)"
  Write-Host "Open:              http://${WebHost}:${WebPort}/"
}
