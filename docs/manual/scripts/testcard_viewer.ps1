# SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
# Ventana WinForms que muestra la testcard a tamaño fijo 1280x960 con
# título conocido ("TubelightTestcard"). Tubelight se ata a este HWND con
# --overlay-target "TubelightTestcard" para procesar SOLO esta imagen,
# evitando capturar el escritorio entero.
#
# Cierre: leer un archivo trigger D:\AgentWorkspace\Tubelight\docs\manual\
# assets\raw\viewer.stop. Cuando aparece, la ventana se cierra.
param(
    [string]$Image = "D:\AgentWorkspace\Tubelight\docs\manual\assets\raw\testcard.png",
    [int]$X = 100,
    [int]$Y = 60,
    [int]$W = 1280,
    [int]$H = 960
)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $Image)) {
    Write-Error "no existe $Image"; exit 1
}

$form = New-Object System.Windows.Forms.Form
$form.Text = "TubelightTestcard"
$form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedSingle
$form.MaximizeBox = $false
$form.MinimizeBox = $false
$form.StartPosition = [System.Windows.Forms.FormStartPosition]::Manual
$form.Location = New-Object System.Drawing.Point $X, $Y
$form.ClientSize = New-Object System.Drawing.Size $W, $H
$form.TopMost = $false
$form.BackColor = [System.Drawing.Color]::Black

$pic = New-Object System.Windows.Forms.PictureBox
$pic.Dock = [System.Windows.Forms.DockStyle]::Fill
$pic.SizeMode = [System.Windows.Forms.PictureBoxSizeMode]::StretchImage
$pic.Image = [System.Drawing.Image]::FromFile($Image)
$form.Controls.Add($pic)

# Polling para cierre limpio
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 400
$stopFile = "D:\AgentWorkspace\Tubelight\docs\manual\assets\raw\viewer.stop"
if (Test-Path $stopFile) { Remove-Item -LiteralPath $stopFile -Force }
$timer.Add_Tick({
    if (Test-Path $stopFile) {
        Remove-Item -LiteralPath $stopFile -Force -ErrorAction SilentlyContinue
        $form.Close()
    }
})
$timer.Start()

$form.Add_Shown({ $form.Activate() })
[System.Windows.Forms.Application]::Run($form)
