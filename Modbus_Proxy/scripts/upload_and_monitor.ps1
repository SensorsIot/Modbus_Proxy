param(
  [string]$Port
)

$ErrorActionPreference = 'Stop'

if (-not (Get-Command pio -ErrorAction SilentlyContinue)) {
  Write-Error "PlatformIO CLI 'pio' not found. Install from https://platformio.org/install and ensure 'pio' is in PATH."
  exit 1
}

$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
  Write-Host "Uploading to ESP32 (env: esp32dev)..."
  $uploadArgs = @('run','-e','esp32dev','-t','upload')
  if ($Port) { $uploadArgs += @('--upload-port', $Port) }
  pio @uploadArgs

  Write-Host "Starting serial monitor at 115200. Press Ctrl+] to exit."
  $monitorArgs = @('device','monitor','-b','115200')
  if ($Port) { $monitorArgs += @('--port', $Port) }
  pio @monitorArgs
}
finally {
  Pop-Location
}

