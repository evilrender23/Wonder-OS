# Wonder Mirror: the program of the Remote Desktop session. It starts the apps
# the host asks for (one small file per request in the shared state folder),
# keeps the catalogue of installed apps fresh, and ends the session once the
# last app window has been closed.
param(
    [string]$State = '\\tsclient\wmstate',
    [string]$Keyboard = '',   # the keyboard layout to type with: 0000040A
    [int]$GraceSeconds = 20
)

$ErrorActionPreference = 'Continue'

# Reconnecting to a session that is still alive runs this again. The newest
# one takes over: the host connects again precisely when the one before
# stopped answering, typically stuck on a shared folder that went away.
$session = (Get-Process -Id $PID).SessionId
Get-CimInstance Win32_Process -Filter "Name = 'powershell.exe'" |
    Where-Object { $_.ProcessId -ne $PID -and $_.SessionId -eq $session -and $_.CommandLine -like '*agent.ps1*' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
# The log lives on the host, next to the session's own, where it can be read.
$log = Join-Path $State 'agent.log'
function Log([string]$text) { Add-Content -Path $log -Value ("{0:s} {1}" -f (Get-Date), $text) }
if ((Test-Path $log) -and (Get-Item $log).Length -gt 256KB) { Remove-Item $log }

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class WonderMirrorWindows
{
    delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] static extern int GetWindowLong(IntPtr window, int index);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder text, int count);
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr window, int attribute, out int value, int size);
    [DllImport("user32.dll")] static extern bool SystemParametersInfo(uint action, uint value, IntPtr pointer, uint flags);

    // Windows arranges windows by itself when one is dragged to an edge of its
    // desktop: it maximises them or snaps them to a half. Here the windows sit
    // on the host's desktop and every one of them is at the top of Windows'
    // own, so a plain drag would end up maximised. Arranging is the host's job.
    public static void LeaveArrangingToHost()
    {
        SystemParametersInfo(0x0083, 0, IntPtr.Zero, 3);   // SPI_SETWINARRANGING, saved and announced
    }

    // The windows a person would call «an open app»: visible, titled, not a
    // tool window, not hidden by the compositor, and not part of the shell.
    public static List<string> Open()
    {
        var found = new List<string>();
        EnumWindows((window, parameter) =>
        {
            if (!IsWindowVisible(window)) return true;
            if ((GetWindowLong(window, -20) & 0x80) != 0) return true;      // WS_EX_TOOLWINDOW
            int cloaked;
            if (DwmGetWindowAttribute(window, 14, out cloaked, 4) == 0 && cloaked != 0) return true;
            var title = new StringBuilder(256);
            if (GetWindowText(window, title, 256) == 0) return true;
            var name = new StringBuilder(256);
            GetClassName(window, name, 256);
            string kind = name.ToString();
            if (kind == "Progman" || kind == "WorkerW" || kind == "Shell_TrayWnd" ||
                kind == "Shell_SecondaryTrayWnd" || kind == "Windows.UI.Core.CoreWindow") return true;
            found.Add(kind + ": " + title);
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@

# Windows greets with questions and offers that mean little in a window on
# another desktop. These are the user's own policy keys: no rights needed, and
# deleting the key gives every answer back to the user.
function Set-Quiet {
    $edge = 'HKCU:\Software\Policies\Microsoft\Edge'
    $values = @{
        HideFirstRunExperience        = 1   # no welcome tour
        DiagnosticData                = 0   # no optional diagnostic data, and no question about it
        DefaultBrowserSettingEnabled  = 0   # no «make Edge the default browser»
        ShowRecommendationsEnabled    = 0   # no feature suggestions
        NewTabPageContentEnabled      = 0   # a new tab without the news feed and its cookie banner
        StartupBoostEnabled           = 0   # Edge kept alive in the background would keep the session open
        BackgroundModeEnabled         = 0
    }
    try {
        if (-not (Test-Path $edge)) { New-Item -Path $edge -Force | Out-Null }
        foreach ($name in $values.Keys) {
            Set-ItemProperty -Path $edge -Name $name -Value $values[$name] -Type DWord
        }
    } catch { Log "quiet: $_" }
}

# Windows would type with the layout the Remote Desktop client announced; the
# host knows which keyboard the user really has.
function Set-Keyboard([string]$layout) {
    try {
        & (Join-Path $PSScriptRoot 'keyboard.ps1') -Layout $layout -State $State
        Log "keyboard $layout"
    } catch { Log "keyboard ${layout}: $_" }
}

function Start-Request([string]$file) {
    try {
        $request = Get-Content -Raw -Encoding UTF8 -Path $file | ConvertFrom-Json
        Remove-Item -Force $file
    } catch {
        Log "bad request ${file}: $_"
        Remove-Item -Force $file -ErrorAction SilentlyContinue
        return
    }
    if ($request.keyboard) { Set-Keyboard $request.keyboard; return }
    try {
        $files = @($request.files | Where-Object { $_ })
        if ($request.id -and $files.Count -gt 0) {
            # Open each file with the chosen app.
            $shell = New-Object -ComObject Shell.Application
            $app = $shell.NameSpace('shell:AppsFolder').Items() | Where-Object { $_.Path -eq $request.id } | Select-Object -First 1
            $target = if ($app) { $app.ExtendedProperty('System.Link.TargetParsingPath') } else { $null }
            foreach ($path in $files) {
                if ($target) { Start-Process -FilePath $target -ArgumentList ('"{0}"' -f $path) }
                else { Start-Process -FilePath $path }
            }
        } elseif ($request.id) {
            Start-Process -FilePath 'explorer.exe' -ArgumentList ('"shell:AppsFolder\{0}"' -f $request.id)
        } else {
            foreach ($path in $files) { Start-Process -FilePath $path }
        }
        Log "started $($request.id) $files"
    } catch {
        Log "could not start $($request.id): $_"
    }
}

# The shared folders appear a moment after the session starts.
for ($i = 0; $i -lt 150 -and -not (Test-Path $State); $i++) { Start-Sleep -Milliseconds 200 }

$requests = Join-Path $State 'requests'
$catalogue = Join-Path $PSScriptRoot 'list-apps.ps1'
$lastActivity = Get-Date
$lastCatalogue = [datetime]::MinValue
[WonderMirrorWindows]::LeaveArrangingToHost()
Log 'session started'
Set-Quiet
if ($Keyboard) { Set-Keyboard $Keyboard }

while ($true) {
    $pending = @(Get-ChildItem -Path $requests -Filter '*.json' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime)
    foreach ($file in $pending) { Start-Request $file.FullName; $lastActivity = Get-Date }

    if (((Get-Date) - $lastCatalogue).TotalMinutes -ge 5) {
        try {
            & $catalogue -Out $State
            $lastCatalogue = Get-Date
        } catch {
            Log "catalogue: $_ $($_.ScriptStackTrace)"
            $lastCatalogue = (Get-Date).AddMinutes(-5).AddSeconds(10)  # again in ten seconds
        }
        $lastActivity = Get-Date
        continue  # requests may have arrived meanwhile
    }

    $open = [WonderMirrorWindows]::Open()
    if ($open.Count -gt 0) { $lastActivity = Get-Date }
    elseif (((Get-Date) - $lastActivity).TotalSeconds -ge $GraceSeconds) { break }
    Start-Sleep -Milliseconds 400
}

Log 'no windows left: ending the session'
logoff
