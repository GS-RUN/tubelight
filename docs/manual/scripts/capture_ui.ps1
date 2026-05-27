# SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
# Capturas interactivas para el manual:
#   - menú in-app abierto en cada tab (Profile/Image/Capture/Audio/Help)
#   - HUD top-right activo
#   - mode fullscreen
#
# Usa el viewer "TubelightTestcard" + --overlay-target.
# Click coords basados en SetNextWindowPos(40,40) + tamaño 520x760 del menú
# y posición del viewer en (100, 60).

param(
    [int]$WaitFirstFrameMs = 3500,
    [int]$WaitSaveMs = 1500,
    [int]$WaitTabMs = 800,
    [string]$ExePath = "D:\AgentWorkspace\Tubelight\tubelight.exe",
    [string]$AssetsRoot = "D:\AgentWorkspace\Tubelight\docs\manual\assets",
    [string]$ViewerScript = "D:\AgentWorkspace\Tubelight\docs\manual\scripts\testcard_viewer.ps1"
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Mouse {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, uint dwExtraInfo);
    public const uint DOWN = 0x0002;
    public const uint UP = 0x0004;
}
"@

# 1) capture_dir
$rawDir = Join-Path $AssetsRoot "raw\captures"
$null = New-Item -ItemType Directory -Path $rawDir -Force
$settings = Join-Path $env:APPDATA "Tubelight\settings.json"
$null = New-Item -ItemType Directory -Path (Split-Path $settings) -Force
@{ capture_dir = $rawDir } | ConvertTo-Json | Set-Content -Encoding utf8 $settings

# 2) Limpieza de orphans previos
Get-Process powershell -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowTitle -eq "TubelightTestcard" } |
    ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
Remove-Item "$rawDir\..\viewer.stop" -Force -ErrorAction SilentlyContinue

# 3) Viewer (visible para asegurar que la testcard aparece)
Write-Host "Lanzando viewer..."
$viewer = Start-Process -FilePath "powershell" -ArgumentList @(
    "-NoProfile","-ExecutionPolicy","Bypass","-File",$ViewerScript
) -PassThru
# Espera hasta 8s a que MainWindowTitle sea correcto
$vp = $null
for ($i=0; $i -lt 16; $i++) {
    Start-Sleep -Milliseconds 500
    $vp = Get-Process -Id $viewer.Id -ErrorAction SilentlyContinue
    if ($vp -and $vp.MainWindowTitle -eq "TubelightTestcard") { break }
}
if (-not $vp -or $vp.MainWindowTitle -ne "TubelightTestcard") {
    Write-Error "viewer no encontrado tras 8s"; exit 1
}
Write-Host ("viewer OK: pid={0} hwnd={1}" -f $vp.Id, $vp.MainWindowHandle)

# Force-bring al frente
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
}
"@
[Win]::ShowWindow($vp.MainWindowHandle, 9)   # SW_RESTORE
[Win]::SetForegroundWindow($vp.MainWindowHandle) | Out-Null
[Win]::BringWindowToTop($vp.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 800

function Send-Hotkey { param([string]$Combo) [System.Windows.Forms.SendKeys]::SendWait($Combo) }

function Click-At {
    param([int]$X, [int]$Y)
    [Mouse]::SetCursorPos($X, $Y)
    Start-Sleep -Milliseconds 80
    [Mouse]::mouse_event([Mouse]::DOWN, 0, 0, 0, 0)
    Start-Sleep -Milliseconds 30
    [Mouse]::mouse_event([Mouse]::UP, 0, 0, 0, 0)
}

function Move-Mouse-Away {
    # Mueve el cursor al borde para que no salga hovereando en la captura.
    [Mouse]::SetCursorPos(20, 1100)
}

function Launch-Tubelight {
    param([string[]]$Args)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $ExePath
    $psi.Arguments = ($Args | ForEach-Object { if ($_ -match "\s") { '"' + $_ + '"' } else { $_ } }) -join ' '
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    return [System.Diagnostics.Process]::Start($psi)
}

function Save-Latest {
    param([string]$Id, [string]$Subdir)
    Start-Sleep -Milliseconds $WaitSaveMs
    $outDir = Join-Path $AssetsRoot $Subdir
    $null = New-Item -ItemType Directory -Path $outDir -Force
    $outPath = Join-Path $outDir "$Id.png"
    $latest = Get-ChildItem -Path $rawDir -Filter "tubelight-*.png" -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($latest) {
        Move-Item -LiteralPath $latest.FullName -Destination $outPath -Force
        Write-Host "  -> $outPath"
        return $true
    }
    Write-Warning "  no PNG generado para $Id"
    return $false
}

# Centros aproximados de cada tab en coordenadas SCREEN.
# Form viewer en (100, 60) outer → client area screen ≈ (108, 90).
# GL window de tubelight (overlay-target) cubre el client area.
# Menú ImGui interno en GL coords (40, 40), title bar ~25, tab bar ~22.
# Screen origen menú = (108+40, 90+40) = (148, 130).
# Tab bar centro vertical ≈ 130 + 25 + 11 = 166.
$tabY  = 168
$tabs = @(
    @{ name = "profile"; x = 188 },
    @{ name = "image";   x = 258 },
    @{ name = "capture"; x = 330 },
    @{ name = "audio";   x = 404 },
    @{ name = "help";    x = 468 }
)

try {
    # ============================================================
    # Series A — menú abierto en cada tab + HUD
    # ============================================================
    Write-Host "`n=== UI menu (5 tabs) + HUD ==="
    $proc = Launch-Tubelight @("--overlay-target","TubelightTestcard","--profile","pvm-8220","--signal","composite_ntsc")
    Start-Sleep -Milliseconds $WaitFirstFrameMs

    # Abrir menú
    Send-Hotkey "^%m"
    Start-Sleep -Milliseconds 700
    # En --overlay-target hay click-through ON por defecto: los clicks
    # cruzan al viewer en vez de llegar al menú. Lo apagamos.
    Send-Hotkey "^%c"
    Start-Sleep -Milliseconds 500
    Move-Mouse-Away

    # Default: Profile tab activo
    Send-Hotkey "^%s"
    [void](Save-Latest "ui-02-menu-profile" "ui")

    $idx = 3
    foreach ($t in $tabs[1..4]) {
        Write-Host "  click tab $($t.name) at ($($t.x),$tabY)"
        Click-At $t.x $tabY
        Start-Sleep -Milliseconds $WaitTabMs
        Move-Mouse-Away
        Send-Hotkey "^%s"
        [void](Save-Latest ("ui-{0:00}-menu-{1}" -f $idx, $t.name) "ui")
        $idx++
    }

    # Reactivar click-through y cerrar menú
    Send-Hotkey "^%c"
    Start-Sleep -Milliseconds 300
    Send-Hotkey "^%m"
    Start-Sleep -Milliseconds 500

    # HUD ON
    Send-Hotkey "^%h"
    Start-Sleep -Milliseconds 600
    Send-Hotkey "^%s"
    [void](Save-Latest "ui-08-hud" "ui")

    # HUD OFF (limpio para futuros)
    Send-Hotkey "^%h"
    Start-Sleep -Milliseconds 200

    try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
    Start-Sleep -Milliseconds 500

    # ============================================================
    # Series B — mode fullscreen
    # ============================================================
    Write-Host "`n=== Mode fullscreen ==="
    $proc2 = Launch-Tubelight @("--overlay-fullscreen","--profile","pvm-8220","--signal","composite_ntsc")
    Start-Sleep -Milliseconds ($WaitFirstFrameMs + 1000)
    Send-Hotkey "^%s"
    [void](Save-Latest "mode-02-fullscreen" "ui")
    try { Stop-Process -Id $proc2.Id -Force -ErrorAction SilentlyContinue } catch {}
    Start-Sleep -Milliseconds 500

    # ============================================================
    # Series C — mode region (rect 800x600 sobre la testcard)
    # ============================================================
    Write-Host "`n=== Mode region ==="
    $proc3 = Launch-Tubelight @("--overlay-region","200,200,800,600","--profile","commodore-1084s","--signal","composite_pal")
    Start-Sleep -Milliseconds $WaitFirstFrameMs
    Send-Hotkey "^%s"
    [void](Save-Latest "mode-04-region" "ui")
    try { Stop-Process -Id $proc3.Id -Force -ErrorAction SilentlyContinue } catch {}
}
finally {
    Write-Host "`nCerrando viewer..."
    Set-Content -Path "$rawDir\..\viewer.stop" -Value "stop" -Force
    Start-Sleep -Seconds 1
    if ($viewer -and -not $viewer.HasExited) {
        try { Stop-Process -Id $viewer.Id -Force -ErrorAction SilentlyContinue } catch {}
    }
}

Write-Host "`nDone. UI shots en $AssetsRoot\ui\"
