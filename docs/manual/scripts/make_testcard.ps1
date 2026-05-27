# SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
# Genera una test-card 1280x960 representativa para mostrar cómo cada
# perfil CRT / signal transforma el mismo contenido.
param(
    [string]$Out = "D:\AgentWorkspace\Tubelight\docs\manual\assets\raw\testcard.png"
)
Add-Type -AssemblyName System.Drawing

$w = 1280; $h = 960
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias

# Background — dark navy field
$g.Clear([System.Drawing.Color]::FromArgb(15, 18, 32))

# Top half: 8 color bars (SMPTE-like)
$colors = @(
    [System.Drawing.Color]::FromArgb(192,192,192),
    [System.Drawing.Color]::FromArgb(192,192,0),
    [System.Drawing.Color]::FromArgb(0,192,192),
    [System.Drawing.Color]::FromArgb(0,192,0),
    [System.Drawing.Color]::FromArgb(192,0,192),
    [System.Drawing.Color]::FromArgb(192,0,0),
    [System.Drawing.Color]::FromArgb(0,0,192),
    [System.Drawing.Color]::FromArgb(0,0,0)
)
$barW = $w / 8
for ($i=0; $i -lt 8; $i++) {
    $br = New-Object System.Drawing.SolidBrush $colors[$i]
    $g.FillRectangle($br, [int]($i * $barW), 0, [int]$barW + 1, 220)
    $br.Dispose()
}

# Center: simulated game-like sprite zone with pixel art block
$pixelSize = 8
$spriteOrigX = 200; $spriteOrigY = 280
$sprite = @(
    "  RRRR  ",
    " RRRRRR ",
    "RRWRWRRR",
    "RRRRRRRR",
    " GGGGGG ",
    " BBBBBB ",
    " BB  BB ",
    "BB    BB"
)
$cmap = @{ 'R' = [System.Drawing.Color]::FromArgb(220, 50, 47);
           'G' = [System.Drawing.Color]::FromArgb(133, 153, 0);
           'B' = [System.Drawing.Color]::FromArgb(38, 139, 210);
           'W' = [System.Drawing.Color]::White }
for ($r = 0; $r -lt $sprite.Count; $r++) {
    for ($c = 0; $c -lt $sprite[$r].Length; $c++) {
        $ch = $sprite[$r][$c]
        if ($ch -ne ' ') {
            $br = New-Object System.Drawing.SolidBrush $cmap[[string]$ch]
            $g.FillRectangle($br, $spriteOrigX + $c * $pixelSize * 4,
                              $spriteOrigY + $r * $pixelSize * 4,
                              $pixelSize * 4, $pixelSize * 4)
            $br.Dispose()
        }
    }
}

# Right side: smooth horizontal black-to-white gradient (manual draw)
$gx0 = 720; $gy0 = 280; $gw = 480; $gh = 280
for ($x = 0; $x -lt $gw; $x++) {
    $v = [int](($x / [double]$gw) * 255)
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb($v,$v,$v))
    $g.DrawLine($pen, $gx0 + $x, $gy0, $gx0 + $x, $gy0 + $gh)
    $pen.Dispose()
}

# Single-pixel white grid to show scanline interaction
$penGrey = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(40,80,80,80)), 1
for ($y = 600; $y -lt 880; $y += 8) {
    $g.DrawLine($penGrey, 60, $y, 1220, $y)
}
$penGrey.Dispose()

# Title text in large white serif
$fontTitle = New-Object System.Drawing.Font "Cambria", 52, ([System.Drawing.FontStyle]::Bold)
$brW = [System.Drawing.Brushes]::White
$g.DrawString("TUBELIGHT", $fontTitle, $brW, 350, 600)
$fontTitle.Dispose()

# Subtitle
$fontSub = New-Object System.Drawing.Font "Consolas", 22
$g.DrawString("CRT overlay  --  test card 1280 x 960  --  v0.1.0", $fontSub,
             [System.Drawing.Brushes]::LightGray, 360, 680)
$fontSub.Dispose()

# Tiny text bottom — readability test for scanlines + mask
$fontTiny = New-Object System.Drawing.Font "Consolas", 12
$line1 = "the quick brown fox jumps over the lazy dog -- THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG"
$line2 = "0123456789 !@#$ () {}[]<>|/ +-_=:; -- 10 PRINT  20 GOTO 10"
$g.DrawString($line1, $fontTiny, [System.Drawing.Brushes]::LightYellow, 60, 800)
$g.DrawString($line2, $fontTiny, [System.Drawing.Brushes]::Cyan, 60, 820)
$fontTiny.Dispose()

# Bottom right: 16-step greyscale ramp
for ($i = 0; $i -lt 16; $i++) {
    $v = [int](($i / 15.0) * 255)
    $br = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb($v,$v,$v))
    $g.FillRectangle($br, 800 + $i * 28, 850, 28, 70)
    $br.Dispose()
}

$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "Wrote $Out"
