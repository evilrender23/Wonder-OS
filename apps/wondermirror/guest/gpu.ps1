# Wonder Mirror, experimental: the driver that lets Windows draw with the
# host's graphics card (see gpu.py on the host).
#
# The driver is alpha software signed with a test certificate, not by
# Microsoft. Windows only loads such a driver in its test-signing mode, so this
# turns that mode on, and `-Action disable` takes all of it back.
#
#   -Action enable | disable | status      -Source <folder with viogpu3d.inf>

param([string]$Action = 'status', [string]$Source = '\\tsclient\wmstate\gpu-driver', [switch]$Elevated)

$ErrorActionPreference = 'Continue'
$Home3D = Join-Path $env:ProgramData 'WonderMirror\gpu'
$Log = Join-Path $Home3D 'gpu.log'
$Report = '\\tsclient\wmstate\gpu.json'
$HardwareId = 'PCI\VEN_1AF4&DEV_1050'
$Policy = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services'

function Get-Status {
    $card = Get-CimInstance Win32_VideoController | Where-Object { $_.PNPDeviceID -like "$HardwareId*" } | Select-Object -First 1
    $signing = (bcdedit /enum '{current}' 2>$null | Select-String -Pattern '^testsigning\s+Yes') -ne $null
    $package = Get-CimInstance Win32_PnPSignedDriver -Filter "DeviceClass = 'DISPLAY'" |
        Where-Object { $_.InfName -like 'oem*.inf' -and $_.HardwareID -like "$HardwareId*" } | Select-Object -First 1
    [ordered]@{
        present     = $card -ne $null                       # the machine has the shared card at all
        name        = if ($card) { $card.Name } else { '' }
        working     = ($card -ne $null) -and ($card.ConfigManagerErrorCode -eq 0) -and ($package -ne $null)
        error       = if ($card) { [int]$card.ConfigManagerErrorCode } else { 0 }
        driver      = if ($package) { $package.DriverVersion } else { '' }
        package     = if ($package) { $package.InfName } else { '' }
        testsigning = $signing
        time        = (Get-Date -Format s)
    }
}

function Write-Report([string]$note) {
    $status = Get-Status
    $status['note'] = $note
    try { $status | ConvertTo-Json | Set-Content -Encoding UTF8 -Path $Report } catch {}
}

if ($Action -eq 'status') { Write-Report ''; return }

if (-not $Elevated) {
    # This half runs as the user, who can read the host's folder; the half that
    # changes Windows needs its permission (the «User Account Control» question).
    New-Item -ItemType Directory -Force -Path $Home3D | Out-Null
    if ($Action -eq 'enable') {
        Remove-Item -Recurse -Force (Join-Path $Home3D 'driver') -ErrorAction SilentlyContinue
        Copy-Item -Recurse -Force $Source (Join-Path $Home3D 'driver')
    }
    Copy-Item -Force $PSCommandPath (Join-Path $Home3D 'gpu.ps1')
    try {
        $run = Start-Process powershell.exe -Verb RunAs -Wait -PassThru -WindowStyle Hidden -ArgumentList @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$(Join-Path $Home3D 'gpu.ps1')`"",
            '-Action', $Action, '-Elevated')
        Write-Report "exit $($run.ExitCode)"
    } catch {
        Write-Report "refused: $_"
    }
    try { Copy-Item -Force $Log '\\tsclient\wmstate\gpu.log' } catch {}
    return
}

Start-Transcript -Path $Log -Append | Out-Null
$driver = Join-Path $Home3D 'driver'
$failed = 0

if ($Action -eq 'enable') {
    $certificate = Join-Path $driver 'VirtIOTestCert.cer'
    certutil -addstore -f Root $certificate
    certutil -addstore -f TrustedPublisher $certificate
    # The alpha driver hangs Windows when the desktop and XAML apps (Settings) draw
    # with it, so remote sessions keep the software renderer as their default adapter;
    # programs that pick the card by name still get it.
    $policy = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services'
    New-Item $policy -Force | Out-Null
    Set-ItemProperty $policy bEnumerateHWBeforeSW 0 -Type DWord
    bcdedit /set testsigning on
    if ($LASTEXITCODE -ne 0) { $failed = 2 }   # Secure Boot is on: the driver would not load
    else {
        pnputil /add-driver (Join-Path $driver 'viogpu3d.inf') /install
        if ($LASTEXITCODE -notin 0, 259, 3010) { $failed = 3 }
    }
    # A Remote Desktop session draws by software unless told otherwise.
    New-Item -Force -Path $Policy | Out-Null
    Set-ItemProperty -Path $Policy -Name bEnumerateHWBeforeSW -Value 1 -Type DWord
}
elseif ($Action -eq 'disable') {
    Get-CimInstance Win32_PnPSignedDriver -Filter "DeviceClass = 'DISPLAY'" |
        Where-Object { $_.InfName -like 'oem*.inf' -and $_.HardwareID -like "$HardwareId*" } |
        ForEach-Object { pnputil /delete-driver $_.InfName /uninstall /force }
    bcdedit /deletevalue testsigning
    Remove-ItemProperty 'HKLM:\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services' bEnumerateHWBeforeSW -ErrorAction SilentlyContinue
    Get-ChildItem Cert:\LocalMachine\Root, Cert:\LocalMachine\TrustedPublisher |
        Where-Object { $_.Subject -like '*VirtIO*Test*' -or $_.Subject -like '*Yttrium*' } | Remove-Item -Force
    Remove-ItemProperty -Path $Policy -Name bEnumerateHWBeforeSW -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $driver -ErrorAction SilentlyContinue
}
Stop-Transcript | Out-Null
exit $failed
