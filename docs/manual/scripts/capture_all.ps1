# SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
# Driver de capturas reproducible para el manual de usuario.
#
# Arranca el viewer WinForms con la testcard (ventana titulada
# "TubelightTestcard", tamaño 1280x960 en X=100,Y=60) y luego, para cada
# combinación de perfil/signal, lanza tubelight con
#   --overlay-target "TubelightTestcard"
# de modo que el pipeline procesa SOLO esa ventana (no el escritorio).
# Dispara Ctrl+Alt+S, espera al guardado, mata el proceso, mueve el PNG.
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File capture_all.ps1
#   powershell -ExecutionPolicy Bypass -File capture_all.ps1 -Only profiles
#
# Requisitos:
#   - tubelight.exe construido
#   - testcard.png ya generada (make_testcard.ps1)

param(
    [string]$Only = "all",   # all | profiles | signals | ui | fine
    [int]$WaitFirstFrameMs = 3500,
    [int]$WaitSaveMs = 1500,
    [string]$ExePath = "D:\AgentWorkspace\Tubelight\tubelight.exe",
    [string]$AssetsRoot = "D:\AgentWorkspace\Tubelight\docs\manual\assets",
    [string]$Testcard = "D:\AgentWorkspace\Tubelight\docs\manual\assets\raw\testcard.png",
    [string]$ViewerScript = "D:\AgentWorkspace\Tubelight\docs\manual\scripts\testcard_viewer.ps1"
)

# 1) capture_dir → carpeta temporal exclusiva
$rawDir = Join-Path $AssetsRoot "raw\captures"
$null = New-Item -ItemType Directory -Path $rawDir -Force
$settings = Join-Path $env:APPDATA "Tubelight\settings.json"
$null = New-Item -ItemType Directory -Path (Split-Path $settings) -Force
@{ capture_dir = $rawDir } | ConvertTo-Json | Set-Content -Encoding utf8 $settings

# 2) testcard.png
if (-not (Test-Path $Testcard)) {
    Write-Host "Generando testcard..."
    & (Join-Path $PSScriptRoot 'make_testcard.ps1') -Out $Testcard | Out-Null
}

# 3) Arranca viewer en background. Esperamos a que aparezca.
Write-Host "Lanzando viewer 'TubelightTestcard'..."
$viewer = Start-Process -FilePath "powershell" -ArgumentList @(
    "-ExecutionPolicy","Bypass",
    "-WindowStyle","Hidden",
    "-File",$ViewerScript,
    "-Image",$Testcard
) -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 3

# Verificar que la ventana del viewer existe vía Get-Process (más fiable
# que FindWindowA con .NET unicode strings).
$viewerProc = Get-Process -Id $viewer.Id -ErrorAction SilentlyContinue
if (-not $viewerProc -or $viewerProc.MainWindowTitle -ne "TubelightTestcard") {
    Start-Sleep -Seconds 2
    $viewerProc = Get-Process -Id $viewer.Id -ErrorAction SilentlyContinue
}
if (-not $viewerProc -or $viewerProc.MainWindowTitle -ne "TubelightTestcard") {
    Write-Error "no se pudo verificar la ventana TubelightTestcard. Aborto."
    exit 1
}
Write-Host ("Viewer OK: pid={0} hwnd={1}" -f $viewerProc.Id, $viewerProc.MainWindowHandle)

Add-Type -AssemblyName System.Windows.Forms

function Send-Hotkey {
    param([string]$Combo)
    [System.Windows.Forms.SendKeys]::SendWait($Combo)
}

function Capture-Shot {
    param(
        [string]$Id,
        [string]$Subdir,
        [string]$Profile,
        [string]$Signal,
        [string[]]$ExtraArgs = @()
    )
    $outDir = Join-Path $AssetsRoot $Subdir
    $null = New-Item -ItemType Directory -Path $outDir -Force
    $outPath = Join-Path $outDir "$Id.png"

    if (Test-Path $outPath) {
        Write-Host "  [skip] $Id (existe)"
        return
    }

    # Modo --overlay-target ata el overlay al HWND de la ventana cuyo
    # título contenga "TubelightTestcard".
    $argList = @("--overlay-target","TubelightTestcard",
                 "--profile",$Profile,"--signal",$Signal) + $ExtraArgs
    Write-Host "  [cap]  $Id   ($Profile / $Signal)"
    # CreateNoWindow oculta la consola del binario (es console-subsystem)
    # para que NO tape la ventana TubelightTestcard que es el target.
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $ExePath
    # Quoting manual de args con espacios
    $psi.Arguments = ($argList | ForEach-Object {
        if ($_ -match "\s") { '"' + $_ + '"' } else { $_ }
    }) -join ' '
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $proc = [System.Diagnostics.Process]::Start($psi)
    Start-Sleep -Milliseconds $WaitFirstFrameMs

    Send-Hotkey "^%s"
    Start-Sleep -Milliseconds $WaitSaveMs

    try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
    Start-Sleep -Milliseconds 500

    $latest = Get-ChildItem -Path $rawDir -Filter "tubelight-*.png" -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($latest) {
        Move-Item -LiteralPath $latest.FullName -Destination $outPath -Force
        Write-Host "    -> $outPath"
    } else {
        Write-Warning "    no PNG generado para $Id"
    }
}

# === Series ===
$profiles = @(
    "pvm-8220","pvm-20m4","sony-bvm-20f1u","sony-fw900",
    "commodore-1084s","sharp-x68k-cz602d","sharp-x68k-cz603d","sharp-cz-614d",
    "nec-multisync-1","nec-multisync-4fg","wells-gardner-k7000","generic-pvm",
    "terminal-p31","terminal-p3-amber","tv-bw-p4","mac-classic-white"
)
$signals = @("rf","composite_ntsc","composite_pal","svideo","scart_rgb","component","rgb_vga")

try {
    if ($Only -in @("all","profiles")) {
        Write-Host "`n=== Galería §6 (16 perfiles CRT) ==="
        foreach ($p in $profiles) {
            Capture-Shot -Id ("prof-" + $p) -Subdir "profiles" `
                         -Profile $p -Signal "composite_ntsc"
        }
    }

    if ($Only -in @("all","signals")) {
        Write-Host "`n=== Galería §7 (7 perfiles signal) ==="
        foreach ($s in $signals) {
            Capture-Shot -Id ("sig-" + $s) -Subdir "signals" `
                         -Profile "pvm-8220" -Signal $s
        }
    }

    if ($Only -in @("all","fine")) {
        Write-Host "`n=== §8 Ajustes finos (comparativas básicas) ==="
        Capture-Shot -Id "fine-flat"      -Subdir "fine" -Profile "generic-pvm" -Signal "rgb_vga"
        Capture-Shot -Id "fine-pvm-ntsc"  -Subdir "fine" -Profile "pvm-8220"    -Signal "composite_ntsc"
        Capture-Shot -Id "fine-rf-noise"  -Subdir "fine" -Profile "pvm-8220"    -Signal "rf"
        Capture-Shot -Id "fine-mono-p31"  -Subdir "fine" -Profile "terminal-p31" -Signal "rgb_vga"
        Capture-Shot -Id "fine-mono-p3"   -Subdir "fine" -Profile "terminal-p3-amber" -Signal "rgb_vga"
        Capture-Shot -Id "fine-mac-1bit"  -Subdir "fine" -Profile "mac-classic-white" -Signal "rgb_vga"
    }

    if ($Only -in @("all","ui")) {
        Write-Host "`n=== §1-§5 (UI + primer overlay) ==="
        Capture-Shot -Id "hero-01"  -Subdir "ui" -Profile "pvm-8220" -Signal "composite_ntsc"
        Capture-Shot -Id "first-02" -Subdir "ui" -Profile "pvm-8220" -Signal "composite_ntsc"
    }
}
finally {
    # Cierra viewer limpiamente
    Write-Host "`nCerrando viewer..."
    Set-Content -Path "$rawDir\..\viewer.stop" -Value "stop" -Force
    Start-Sleep -Seconds 1
    if ($viewer -and -not $viewer.HasExited) {
        try { Stop-Process -Id $viewer.Id -Force -ErrorAction SilentlyContinue } catch {}
    }
}

Write-Host "`nDone. Capturas en $AssetsRoot."
