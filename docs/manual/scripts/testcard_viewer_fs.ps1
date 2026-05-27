# SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
# Viewer fullscreen — testcard ocupando todo el monitor.
param(
    [string]$Image = "D:\AgentWorkspace\Tubelight\docs\manual\assets\raw\testcard.png"
)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$form = New-Object System.Windows.Forms.Form
$form.Text = "TubelightTestcard"
$form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::None
$form.WindowState = [System.Windows.Forms.FormWindowState]::Maximized
$form.TopMost = $false
$form.BackColor = [System.Drawing.Color]::Black

$pic = New-Object System.Windows.Forms.PictureBox
$pic.Dock = [System.Windows.Forms.DockStyle]::Fill
$pic.SizeMode = [System.Windows.Forms.PictureBoxSizeMode]::Zoom
$pic.Image = [System.Drawing.Image]::FromFile($Image)
$form.Controls.Add($pic)

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
