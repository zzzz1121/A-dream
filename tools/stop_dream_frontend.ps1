param(
  [string]$WebHost = "127.0.0.1",
  [int]$WebPort = 8765,
  [switch]$AllowUnknownProcess
)

$ErrorActionPreference = "Stop"

function Get-ProcessCommandLine {
  param([int]$ProcessId)

  try {
    $process = Get-CimInstance Win32_Process -Filter "ProcessId = $ProcessId" -ErrorAction Stop
    return [string]$process.CommandLine
  } catch {
    return ""
  }
}

$connections = @(Get-NetTCPConnection -LocalAddress $WebHost -LocalPort $WebPort -State Listen -ErrorAction SilentlyContinue)
if ($connections.Count -eq 0) {
  Write-Host "Dream frontend is not listening at http://${WebHost}:${WebPort}/"
  exit 0
}

$pids = @($connections | Select-Object -ExpandProperty OwningProcess -Unique)
$stopped = 0

foreach ($pid in $pids) {
  if (-not $pid) {
    continue
  }

  $process = Get-Process -Id $pid -ErrorAction SilentlyContinue
  if ($null -eq $process) {
    continue
  }

  $commandLine = Get-ProcessCommandLine -ProcessId $pid
  $looksLikeDream = $commandLine -match "dream_eeg_serial_bridge\.py"

  if (-not $looksLikeDream -and -not $AllowUnknownProcess) {
    Write-Host "Found listener on http://${WebHost}:${WebPort}/, but it does not look like Dream frontend."
    Write-Host "PID:          $pid"
    Write-Host "Process:      $($process.ProcessName)"
    if ($commandLine) {
      Write-Host "Command line: $commandLine"
    } else {
      Write-Host "Command line: unavailable"
    }
    Write-Host "Use -AllowUnknownProcess to stop it anyway."
    continue
  }

  Write-Host "Stopping Dream frontend..."
  Write-Host "PID:     $pid"
  Write-Host "Process: $($process.ProcessName)"
  Stop-Process -Id $pid -Force
  $stopped++
}

if ($stopped -eq 0) {
  Write-Host "No Dream frontend process was stopped."
} else {
  Start-Sleep -Milliseconds 300
  $remaining = @(Get-NetTCPConnection -LocalAddress $WebHost -LocalPort $WebPort -State Listen -ErrorAction SilentlyContinue)
  if ($remaining.Count -eq 0) {
    Write-Host "Dream frontend stopped."
  } else {
    Write-Host "Stop command sent, but something is still listening at http://${WebHost}:${WebPort}/"
  }
}
