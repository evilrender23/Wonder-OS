# Wonder Mirror: runs once, at the first logon of a fresh Windows, and turns it
# into a machine whose apps can be shown as windows of the Wonderland desktop.
# Nothing here is specific to one host: the scripts used afterwards are read
# from the host through the redirected \\tsclient\wondermirror share.

$ErrorActionPreference = 'Continue'
$log = Join-Path $env:ProgramData 'WonderMirror\setup.log'
New-Item -ItemType Directory -Force -Path (Split-Path $log) | Out-Null
Start-Transcript -Path $log -Append | Out-Null

# Remote Desktop, the channel every window travels through. Network Level
# Authentication stays on.
$ts = 'HKLM:\SYSTEM\CurrentControlSet\Control\Terminal Server'
Set-ItemProperty -Path $ts -Name fDenyTSConnections -Value 0 -Type DWord
Set-ItemProperty -Path "$ts\WinStations\RDP-Tcp" -Name UserAuthentication -Value 1 -Type DWord
# Rule names, unlike their display group, are the same in every language.
Enable-NetFirewallRule -Name 'RemoteDesktop-UserMode-In-TCP', 'RemoteDesktop-UserMode-In-UDP' -ErrorAction SilentlyContinue

# RemoteApp: let any installed program be started as a remote window.
$allow = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Terminal Server\TSAppAllowList'
New-Item -Force -Path $allow | Out-Null
Set-ItemProperty -Path $allow -Name fDisabledAllowList -Value 1 -Type DWord
$policy = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services'
New-Item -Force -Path $policy | Out-Null
Set-ItemProperty -Path $policy -Name fAllowUnlistedRemotePrograms -Value 1 -Type DWord
# A window that loses its connection keeps its program running, so reconnecting
# brings back the work that was open.
Set-ItemProperty -Path $policy -Name MaxDisconnectionTime -Value 0 -Type DWord
Set-ItemProperty -Path $policy -Name RemoteAppLogoffTimeLimit -Value 0 -Type DWord

# The machine has no screen of its own: never sleep, hibernate or lock.
powercfg /hibernate off
powercfg /change standby-timeout-ac 0
powercfg /change monitor-timeout-ac 0
powercfg /change disk-timeout-ac 0
Set-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Power' -Name HiberbootEnabled -Value 0 -Type DWord

# The account's password is a long random one kept by the host; it must not expire.
Set-LocalUser -Name $env:USERNAME -PasswordNeverExpires $true -ErrorAction SilentlyContinue

Set-Content -Path (Join-Path $env:ProgramData 'WonderMirror\ready') -Value (Get-Date -Format o)
Stop-Transcript | Out-Null
