# SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
# Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
#
# Enforces Constitution C1: no absolute personal paths in versioned files.
# Usage: powershell -ExecutionPolicy Bypass -File tools/precommit_check_paths.ps1
# Exit 0 if clean, 1 if violations found.

$ErrorActionPreference = 'Stop'

$dirs = @('src','specs','docs','schemas','profiles','tests','tools','shaders','.github')
$extensions = @('*.md','*.cpp','*.cc','*.h','*.hpp','*.json','*.glsl','*.frag','*.vert','*.comp','*.cmake','CMakeLists.txt','*.txt','*.yml','*.yaml','*.toml','*.sh','*.ps1')

$pattern = '([A-Z]:\\[Uu]sers\\|/home/[A-Za-z0-9._-]+/|/Users/[A-Za-z0-9._-]+/)'
$whitelist = '(/home/runner/|C:.Users.runneradmin|/home/<user>|C:.Users.<user>|/Users/<user>|pathcheck-allow)'

$excludePaths = @(
    'tools/precommit_check_paths.sh',
    'tools/precommit_check_paths.ps1',
    'specs/CONSTITUTION.LOCKED.md',
    'docs/adr/README.md'
)

function Is-Excluded($path) {
    $relative = $path -replace [regex]::Escape((Get-Location).Path + [IO.Path]::DirectorySeparatorChar), ''
    $relative = $relative -replace '\\', '/'
    return $excludePaths -contains $relative
}

$files = @()
foreach ($d in $dirs) {
    if (Test-Path $d) {
        foreach ($e in $extensions) {
            $files += Get-ChildItem -Path $d -Recurse -File -Filter $e -ErrorAction SilentlyContinue
        }
    }
}

if ($files.Count -eq 0) {
    Write-Host "[precommit] no files to scan."
    exit 0
}

$hits = @()
foreach ($f in $files) {
    if (Is-Excluded $f.FullName) { continue }
    $matches = Select-String -Path $f.FullName -Pattern $pattern -AllMatches
    foreach ($m in $matches) {
        if ($m.Line -notmatch $whitelist) {
            $hits += "$($f.FullName):$($m.LineNumber):$($m.Line.Trim())"
        }
    }
}

if ($hits.Count -gt 0) {
    Write-Host "ERROR (C1 violation): absolute personal paths found in versioned files:"
    $hits | ForEach-Object { Write-Host "  $_" }
    Write-Host ""
    Write-Host "Fix: replace with relative paths, env vars (e.g. `$HOME, %APPDATA%), or a placeholder."
    exit 1
}

Write-Host "[precommit] OK -- no personal paths in versioned files."
exit 0
