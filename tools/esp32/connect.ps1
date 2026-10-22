
param(
    [string]$BusId
)

if (-not (Get-Command usbipd -ErrorAction SilentlyContinue)) {
    throw "usbipd is not installed."
}

# Find the Silicon Labs CP210x USB adapter
if (-not $BusId) {
    $devices = @(usbipd list | Where-Object {
        $_ -match 'Silicon Labs|CP210x|10c4:ea60'
    })

    if ($devices.Count -ne 1) {
        usbipd list
        throw "Expected one CP210x device. Specify -BusId manually."
    }

    if ($devices[0] -notmatch '^\s*(\d+-\d+)\s+') {
        throw "Could not identify the USB BUSID."
    }

    $BusId = $Matches[1]
}

Write-Host "ESP32 USB BUSID: $BusId"

# First-time setup: run PowerShell as Administrator
usbipd bind --busid $BusId
if ($LASTEXITCODE -ne 0) {
    throw "USB bind failed. Run as Administrator for initial setup."
}

# Attach USB device to WSL2
usbipd attach --wsl --busid $BusId --auto-attach
if ($LASTEXITCODE -ne 0) {
    throw "USB attach failed."
}

Write-Host "ESP32 attached to WSL2."
