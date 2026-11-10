# Wonder Mirror: makes Windows type with one keyboard layout, the one chosen on
# the host. Every app already open switches to it, the ones started later get
# it too, and the answer is left in the shared state folder for the host.
param(
    [Parameter(Mandatory = $true)][string]$Layout,   # a keyboard layout identifier: 0000040A
    [string]$State = '\\tsclient\wmstate'
)

$ErrorActionPreference = 'Stop'

if (-not ('WonderMirrorKeyboard' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

public static class WonderMirrorKeyboard
{
    delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr window, uint message, IntPtr wide, IntPtr value);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern IntPtr LoadKeyboardLayout(string name, uint flags);
    [DllImport("user32.dll")] static extern bool UnloadKeyboardLayout(IntPtr layout);
    [DllImport("user32.dll")] static extern int GetKeyboardLayoutList(int count, [Out] IntPtr[] list);
    [DllImport("user32.dll")] static extern bool SystemParametersInfo(uint action, uint value, ref IntPtr pointer, uint flags);

    public static List<string> Loaded()
    {
        var list = new IntPtr[64];
        int count = GetKeyboardLayoutList(list.Length, list);
        var found = new List<string>();
        for (int i = 0; i < count; i++) found.Add((list[i].ToInt64() & 0xFFFFFFFF).ToString("X8"));
        return found;
    }

    public static string Use(string name)
    {
        IntPtr layout = LoadKeyboardLayout(name, 0x3);                  // KLF_ACTIVATE | KLF_SUBSTITUTE_OK
        // Asked for a layout it cannot load, Windows answers with the one in
        // use. The low half of what it loads is always the language asked for.
        if (layout == IntPtr.Zero || (layout.ToInt64() & 0xFFFF) != Convert.ToInt64(name.Substring(4), 16))
            throw new InvalidOperationException("Windows cannot load the layout " + name);
        // What a program started from now on begins with. Not written to the
        // user's profile: the host says it again in every session.
        SystemParametersInfo(0x005A, 0, ref layout, 2);                 // SPI_SETDEFAULTINPUTLANG, announced
        // Each window keeps a layout of its own: ask every one to change.
        EnumWindows((window, parameter) =>
        {
            PostMessage(window, 0x0050, IntPtr.Zero, layout);           // WM_INPUTLANGCHANGEREQUEST
            return true;
        }, IntPtr.Zero);
        Thread.Sleep(300);
        // Leave no other layout loaded: Alt+Shift, pressed by accident, would
        // switch to it, and there is no language bar here to notice it on.
        var list = new IntPtr[64];
        int count = GetKeyboardLayoutList(list.Length, list);
        for (int i = 0; i < count; i++) if (list[i] != layout) UnloadKeyboardLayout(list[i]);
        return (layout.ToInt64() & 0xFFFFFFFF).ToString("X8");
    }
}
'@
}

$answer = [ordered]@{ layout = $Layout.ToUpper(); active = ''; loaded = @(); error = '' }
try {
    if ($Layout -notmatch '^[0-9a-fA-F]{8}$' -or
        -not (Test-Path "HKLM:\SYSTEM\CurrentControlSet\Control\Keyboard Layouts\$Layout")) {
        throw "Windows has no keyboard layout $Layout"
    }
    # A language of the user may already type with this layout (Spanish of
    # Spain is 00000C0A, typing with 0000040A): load it under that name, so
    # Windows keeps showing the language the user knows.
    $name = $Layout
    $preload = Get-ItemProperty 'HKCU:\Keyboard Layout\Preload' -ErrorAction SilentlyContinue
    $substitutes = Get-ItemProperty 'HKCU:\Keyboard Layout\Substitutes' -ErrorAction SilentlyContinue
    if ($preload) {
        foreach ($entry in $preload.PSObject.Properties | Where-Object { $_.Name -match '^\d+$' }) {
            $real = if ($substitutes -and $substitutes.($entry.Value)) { $substitutes.($entry.Value) } else { $entry.Value }
            if ($real -ieq $Layout) { $name = $entry.Value; break }
        }
    }
    try { $answer.active = [WonderMirrorKeyboard]::Use($name) }
    catch { if ($name -ieq $Layout) { throw }; $answer.active = [WonderMirrorKeyboard]::Use($Layout) }
    $answer.loaded = @([WonderMirrorKeyboard]::Loaded())
} catch {
    $answer.error = "$($_.Exception.Message)"
}
# Written whole and then renamed: the host reads it the moment it appears.
$target = Join-Path $State 'keyboard.json'
$answer | ConvertTo-Json -Compress | Set-Content -Encoding UTF8 -Path "$target.new"
Move-Item -Force -Path "$target.new" -Destination $target
if ($answer.error) { throw $answer.error }
