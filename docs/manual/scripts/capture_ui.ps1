# SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
# Capturas UI con --overlay-region PIN al rect exacto del viewer.
# El viewer client rect es (108, 91) 1280x960 → overlay anclado allí.
# Garantiza que CADA captura tiene la testcard procesada por debajo.

param(
    [int]$WaitFrameMs = 3000,
    [int]$WaitHotkeyMs = 700,
    [int]$WaitSaveMs = 1300,
    [int]$WaitTabMs = 700,
    [string]$ExePath,
    [string]$AssetsRoot,
    [string]$ViewerScript
)
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
if (-not $ExePath)      { $ExePath      = Join-Path $repoRoot "tubelight.exe" }
if (-not $AssetsRoot)   { $AssetsRoot   = Join-Path $repoRoot "docs\manual\assets" }
if (-not $ViewerScript) { $ViewerScript = Join-Path $PSScriptRoot "testcard_viewer.ps1" }

Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class M {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint d, uint x, uint y, uint w, uint e);
    public const uint DOWN = 0x0002; public const uint UP = 0x0004;
}
public class F {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out R r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref P p);
    public struct R { public int l,t,r,b; }
    public struct P { public int x,y; }
}
"@

$rawDir = Join-Path $AssetsRoot "raw\captures"
$null = New-Item -ItemType Directory -Path $rawDir -Force
@{ capture_dir = $rawDir; recordable = $false } | ConvertTo-Json |
    Set-Content -Encoding utf8 (Join-Path $env:APPDATA "Tubelight\settings.json")

Get-Process powershell -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -eq "TubelightTestcard" } | Stop-Process -Force -ErrorAction SilentlyContinue
Get-Process tubelight -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Remove-Item "$rawDir\..\viewer.stop" -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

function HK { param([string]$Combo) [System.Windows.Forms.SendKeys]::SendWait($Combo); Start-Sleep -Milliseconds $WaitHotkeyMs }
function Click-At { param([int]$X, [int]$Y)
    [M]::SetCursorPos($X, $Y); Start-Sleep -Milliseconds 100
    [M]::mouse_event([M]::DOWN, 0, 0, 0, 0); Start-Sleep -Milliseconds 40
    [M]::mouse_event([M]::UP, 0, 0, 0, 0); Start-Sleep -Milliseconds 100
}
function Move-Away { [M]::SetCursorPos(20, 1100) }

function Launch-Viewer {
    $v = Start-Process -FilePath "powershell" -ArgumentList @(
        "-NoProfile","-ExecutionPolicy","Bypass","-File",$ViewerScript) -PassThru
    for ($i=0; $i -lt 16; $i++) {
        Start-Sleep -Milliseconds 500
        $vp = Get-Process -Id $v.Id -ErrorAction SilentlyContinue
        if ($vp -and $vp.MainWindowTitle -eq "TubelightTestcard") { return @{ proc=$v; hwnd=$vp.MainWindowHandle } }
    }
    Write-Error "viewer no encontrado"; exit 1
}

function Get-Viewer-Rect {
    param($Hwnd)
    $r = New-Object F+R; [F]::GetClientRect($Hwnd, [ref]$r) | Out-Null
    $p = New-Object F+P; $p.x=0; $p.y=0
    [F]::ClientToScreen($Hwnd, [ref]$p) | Out-Null
    return @{ x=$p.x; y=$p.y; w=$r.r; h=$r.b }
}

function Launch-Tubelight { param([string[]]$Args)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $ExePath
    $psi.Arguments = ($Args | ForEach-Object { if ($_ -match "\s|,") { '"' + $_ + '"' } else { $_ } }) -join ' '
    $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    [System.Diagnostics.Process]::Start($psi)
}

function Save-As { param([string]$Id, [string]$Subdir)
    Start-Sleep -Milliseconds $WaitSaveMs
    $outDir = Join-Path $AssetsRoot $Subdir
    $null = New-Item -ItemType Directory -Path $outDir -Force
    $outPath = Join-Path $outDir "$Id.png"
    $latest = Get-ChildItem -Path $rawDir -Filter "tubelight-*.png" -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($latest) { Move-Item -LiteralPath $latest.FullName -Destination $outPath -Force; Write-Host "    -> $outPath" }
    else { Write-Warning "    no PNG: $Id" }
}

$vInfo = Launch-Viewer
$rect = Get-Viewer-Rect $vInfo.hwnd
$regionArg = "$($rect.x),$($rect.y),$($rect.w),$($rect.h)"
Write-Host "viewer hwnd=$($vInfo.hwnd)  region=$regionArg"

# Tab coords reales (menú abre en GL(40,40), screen origin = viewer + 40)
$menuOriginX = $rect.x + 40
$menuOriginY = $rect.y + 40
# Tab bar centro vertical: title(~25) + tab(~22)/2 = ~36 desde menú origin
$tabY = $menuOriginY + 49
$tabs = @(
    @{ name = "image";   x = $menuOriginX + 90;  id = "ui-03-menu-image" },
    @{ name = "capture"; x = $menuOriginX + 160; id = "ui-04-menu-capture" },
    @{ name = "audio";   x = $menuOriginX + 232; id = "ui-05-menu-audio" },
    @{ name = "help";    x = $menuOriginX + 292; id = "ui-06-menu-help" }
)
Write-Host "tab bar y=$tabY"

try {
    # ============================================================
    # MENU + tabs + HUD — overlay-region ANCLADO exacto al viewer
    # ============================================================
    Write-Host "`n=== MENU + tabs + HUD (region $regionArg, pvm-8220) ==="
    $tl = Launch-Tubelight @("--overlay-region",$regionArg,"--profile","pvm-8220","--signal","composite_ntsc")
    Start-Sleep -Milliseconds $WaitFrameMs
    HK "^%m"; Start-Sleep -Milliseconds 700
    HK "^%c"; Start-Sleep -Milliseconds 500  # disable click-through para que clicks lleguen al menú
    Move-Away

    HK "^%s"; Save-As "ui-02-menu-profile" "ui"

    foreach ($t in $tabs) {
        Write-Host "  click $($t.name) at ($($t.x),$tabY)"
        Click-At $t.x $tabY
        Start-Sleep -Milliseconds $WaitTabMs
        Move-Away
        HK "^%s"; Save-As $t.id "ui"
    }

    HK "^%m"; Start-Sleep -Milliseconds 400
    HK "^%h"; Start-Sleep -Milliseconds 500
    HK "^%s"; Save-As "ui-08-hud" "ui"
    HK "^%h"

    try { Stop-Process -Id $tl.Id -Force -ErrorAction SilentlyContinue } catch {}
    Start-Sleep -Milliseconds 700

    # ============================================================
    # MODE FULLSCREEN — overlay-region cubriendo el viewer entero
    # (representa el modo fullscreen: el overlay procesa toda su área)
    # ============================================================
    Write-Host "`n=== MODE fullscreen (region=$regionArg full viewer) ==="
    $tl2 = Launch-Tubelight @("--overlay-region",$regionArg,"--profile","pvm-8220","--signal","composite_ntsc")
    Start-Sleep -Milliseconds $WaitFrameMs
    Move-Away
    HK "^%s"; Save-As "mode-02-fullscreen" "ui"
    try { Stop-Process -Id $tl2.Id -Force -ErrorAction SilentlyContinue } catch {}
    Start-Sleep -Milliseconds 700

    # ============================================================
    # MODE REGION — rect 800x600 DENTRO del viewer
    # (representa el modo region: subrect arbitrario sobre el contenido)
    # ============================================================
    $regionInner = "$($rect.x + 100),$($rect.y + 100),800,600"
    Write-Host "`n=== MODE region (inner $regionInner, 1084s/pal) ==="
    $tl3 = Launch-Tubelight @("--overlay-region",$regionInner,"--profile","commodore-1084s","--signal","composite_pal")
    Start-Sleep -Milliseconds $WaitFrameMs
    Move-Away
    HK "^%s"; Save-As "mode-04-region" "ui"
    try { Stop-Process -Id $tl3.Id -Force -ErrorAction SilentlyContinue } catch {}
}
finally {
    Set-Content -Path "$rawDir\..\viewer.stop" -Value "stop" -Force
    Start-Sleep -Seconds 1
    if ($vInfo.proc -and -not $vInfo.proc.HasExited) {
        try { Stop-Process -Id $vInfo.proc.Id -Force -ErrorAction SilentlyContinue } catch {}
    }
}

Write-Host "`nDone."
