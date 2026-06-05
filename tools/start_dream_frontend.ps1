param(
  [string]$Source = "COM10",
  [string]$Target = "COM6",
  [int]$SourceBaud = 9600,
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

function Use-AvailableLogPath {
  param([string]$Path)

  if (!(Test-Path $Path)) {
    return $Path
  }

  try {
    Clear-Content -LiteralPath $Path -ErrorAction Stop
    return $Path
  } catch {
    $directory = Split-Path -Parent $Path
    $name = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    $extension = [System.IO.Path]::GetExtension($Path)
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $fallback = Join-Path $directory "$name.$timestamp$extension"
    Write-Host "Log file is busy, using: $fallback"
    return $fallback
  }
}

function Get-SerialPortNames {
  try {
    return @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
  } catch {
    Write-Host "Unable to enumerate serial ports: $($_.Exception.Message)"
    return @()
  }
}

$outLog = Use-AvailableLogPath $outLog
$errLog = Use-AvailableLogPath $errLog

$serialPorts = Get-SerialPortNames
if ($serialPorts.Count -gt 0) {
  Write-Host "Available serial ports: $($serialPorts -join ', ')"
} else {
  Write-Host "Available serial ports: none detected"
}
if ($serialPorts.Count -gt 0 -and $serialPorts -notcontains $Source) {
  Write-Host "WARNING: Source EEG port $Source is not in the current Windows COM list."
}
if ($serialPorts.Count -gt 0 -and $serialPorts -notcontains $Target) {
  Write-Host "WARNING: Target M5 port $Target is not in the current Windows COM list."
}

$existing = Get-NetTCPConnection -LocalAddress $WebHost -LocalPort $WebPort -State Listen -ErrorAction SilentlyContinue
if ($existing) {
  Write-Host "Dream frontend already appears to be listening at http://${WebHost}:${WebPort}/"
  Write-Host "PID: $($existing.OwningProcess)"
  exit 0
}

$arguments = @(
  "-u",
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
  Write-Host "EEG fresh/usable:  $($state.eeg.fresh) / $($state.eeg.usable)"
  Write-Host "EEG age:           $($state.eeg.ageMs) ms"
  Write-Host "EEG last usable:   $($state.eeg.lastUsableAgeMs) ms"
  Write-Host "EEG packets:       raw $($state.stats.rawBytes) / valid $($state.stats.validPackets) / usable $($state.stats.usablePackets)"
  Write-Host "EEG values:        poorSignal $($state.eeg.poorSignal), attention $($state.eeg.attention), meditation $($state.eeg.meditation)"
  Write-Host "EEG diagnosis:     $($state.eeg.diagnosis)"
  Write-Host "M5 seen:           $($state.m5.seen)"
  Write-Host "Microduino seen:   $($state.mic.seen)"
} catch {
  Write-Host "Process started, but health check did not respond yet."
  Write-Host "PID:               $($process.Id)"
  Write-Host "Open:              http://${WebHost}:${WebPort}/"
}
