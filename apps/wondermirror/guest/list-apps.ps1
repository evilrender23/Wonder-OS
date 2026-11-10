# Wonder Mirror: writes the catalogue of installed Windows apps, with their
# icons, where the host can read it. The Start menu's own list
# (shell:AppsFolder) is the source, so classic programs and Store apps come out
# the same way and each can be started with «explorer shell:AppsFolder\<id>».
param([string]$Out = '\\tsclient\wmstate')

$ErrorActionPreference = 'Stop'

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class WonderMirrorIconReader
{
    [ComImport, Guid("bcc18b79-ba16-442f-80c4-8a59c30c463b"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IShellItemImageFactory
    {
        [PreserveSig] int GetImage(SIZE size, int flags, out IntPtr bitmap);
    }

    [StructLayout(LayoutKind.Sequential)]
    struct SIZE { public int cx; public int cy; }

    [StructLayout(LayoutKind.Sequential)]
    struct BITMAP
    {
        public int bmType, bmWidth, bmHeight, bmWidthBytes;
        public short bmPlanes, bmBitsPixel;
        public IntPtr bmBits;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct BITMAPINFOHEADER
    {
        public int biSize, biWidth, biHeight;
        public short biPlanes, biBitCount;
        public int biCompression, biSizeImage, biXPelsPerMeter, biYPelsPerMeter, biClrUsed, biClrImportant;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct DIBSECTION
    {
        public BITMAP dsBm;
        public BITMAPINFOHEADER dsBmih;
        public int dsBitfields0, dsBitfields1, dsBitfields2;
        public IntPtr dshSection;
        public int dsOffset;
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
    static extern void SHCreateItemFromParsingName(string path, IntPtr context, ref Guid iid,
        [MarshalAs(UnmanagedType.Interface)] out IShellItemImageFactory item);

    [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr handle);
    [DllImport("gdi32.dll")] static extern int GetObject(IntPtr handle, int size, out DIBSECTION bitmap);

    // Saves the item's icon as a PNG with its transparency.
    public static void Save(string parsingName, string file, int size)
    {
        Guid iid = typeof(IShellItemImageFactory).GUID;
        IShellItemImageFactory factory;
        SHCreateItemFromParsingName(parsingName, IntPtr.Zero, ref iid, out factory);
        IntPtr handle;
        int result = factory.GetImage(new SIZE { cx = size, cy = size }, 0x4 /* icon only */, out handle);
        if (result != 0) Marshal.ThrowExceptionForHR(result);
        try
        {
            DIBSECTION section;
            int got = GetObject(handle, Marshal.SizeOf(typeof(DIBSECTION)), out section);
            BITMAP info = section.dsBm;
            if (info.bmBitsPixel != 32 || info.bmBits == IntPtr.Zero)
            {
                using (Bitmap plain = Image.FromHbitmap(handle)) plain.Save(file, ImageFormat.Png);
                return;
            }
            // Image.FromHbitmap drops the alpha channel: wrap the pixels instead.
            // The shell hands out a bottom-up DIB (positive biHeight): its first
            // row in memory is the bottom of the picture, so read it backwards
            // or the icon comes out upside down.
            bool bottomUp = got >= Marshal.SizeOf(typeof(DIBSECTION)) && section.dsBmih.biHeight > 0;
            int stride = info.bmWidthBytes;
            IntPtr first = info.bmBits;
            if (bottomUp)
            {
                first = new IntPtr(info.bmBits.ToInt64() + (long)(info.bmHeight - 1) * stride);
                stride = -stride;
            }
            using (Bitmap view = new Bitmap(info.bmWidth, info.bmHeight, stride,
                                            PixelFormat.Format32bppPArgb, first))
            using (Bitmap copy = new Bitmap(view))
            {
                copy.Save(file, ImageFormat.Png);
            }
        }
        finally { DeleteObject(handle); }
    }
}
'@

# Start menu entries that are not apps: uninstallers, manuals, web links.
$noise = '(?i)(uninstall|desinstalar|readme|l[eé]eme|license|licencia|release notes|documentation|documentaci[oó]n|website|sitio web|manual)'
$notPrograms = '(?i)\.(url|txt|chm|htm|html|pdf|rtf|ini|log|website)$'

$icons = Join-Path $Out 'icons'
New-Item -ItemType Directory -Force -Path $icons | Out-Null
$sha = [System.Security.Cryptography.SHA1]::Create()
$iconRevision = 'icons-2'  # bump whenever the way icons are drawn changes

$folder = (New-Object -ComObject Shell.Application).NameSpace('shell:AppsFolder')
$apps = foreach ($item in $folder.Items()) {
    $id = $item.Path
    $name = $item.Name
    if (-not $id -or -not $name) { continue }
    if ($name -match $noise -or $id -match $notPrograms -or $id -match '^https?:') { continue }
    # The revision is part of the name so icons drawn by an older, wrong version
    # of this script are not reused (nor served from the shell's image cache).
    $digest = -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes("$iconRevision`n$id")) | ForEach-Object { $_.ToString('x2') })
    $icon = "$digest.png"
    $file = Join-Path $icons $icon
    if (-not (Test-Path $file)) {
        try { [WonderMirrorIconReader]::Save("shell:AppsFolder\$id", $file, 256) } catch { $icon = '' }
    }
    [pscustomobject]@{ id = $id; name = $name; icon = $icon }
}

$json = ConvertTo-Json -InputObject @($apps | Sort-Object name) -Depth 3
$temporary = Join-Path $Out 'apps.json.new'
[IO.File]::WriteAllText($temporary, $json, (New-Object Text.UTF8Encoding $false))
Move-Item -Force $temporary (Join-Path $Out 'apps.json')

# Icons of apps that are gone, or drawn by an older revision of this script.
$used = @{}
foreach ($app in $apps) { if ($app.icon) { $used[$app.icon] = $true } }
if ($used.Count -gt 0) {
    Get-ChildItem -Path $icons -Filter '*.png' -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^[0-9a-f]{40}\.png$' -and -not $used.ContainsKey($_.Name) } |
        Remove-Item -Force -ErrorAction SilentlyContinue
}
