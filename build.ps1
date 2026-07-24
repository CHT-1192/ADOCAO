#!/usr/bin/env pwsh
<#
.SYNOPSIS
    ADOCAO build script — interactive or CLI mode.

.DESCRIPTION
    Without arguments, prompts interactively for each build option.
    With arguments, runs non-interactively (matching legacy build.bat behavior).

.PARAMETER Portable
    Static-linked portable build (no MinGW DLLs required).

.PARAMETER ExZoom
    Enable extra zoom (min zoom 0.5 instead of 5.0).

.PARAMETER Generator
    CMake generator: "MinGW Makefiles" (default on Windows), "Ninja", "Visual Studio 17 2022".
    Auto-detected if not specified.

.PARAMETER BuildType
    CMake build type. Default: Release.

.EXAMPLE
    ./build.ps1                          # interactive mode
    ./build.ps1 -Portable -ExZoom        # CLI: static + extra zoom
#>

param(
    [switch]$Portable,
    [switch]$ExZoom,
    [string]$Generator,
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

# ── Colour helpers ──────────────────────────────────────────────
function Write-Cyan  { Write-Host $args -ForegroundColor Cyan }
function Write-Green { Write-Host $args -ForegroundColor Green }
function Write-Yellow{ Write-Host $args -ForegroundColor Yellow }
function Write-Dim   { Write-Host $args -ForegroundColor DarkGray }

# ── Compiler detection ──────────────────────────────────────────
function Find-Compiler {
    # MinGW (winget)
    $wingetBase = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages"
    if (Test-Path $wingetBase) {
        $dirs = Get-ChildItem $wingetBase -Directory |
                Where-Object { $_.Name -like "*WinLibs*" } |
                Sort-Object Name -Descending
        foreach ($d in $dirs) {
            $gxx = Join-Path $d.FullName "mingw64\bin\g++.exe"
            if (Test-Path $gxx) { return @{ CXX = $gxx; CC = (Join-Path $d.FullName "mingw64\bin\gcc.exe"); Kind = "G++ (MinGW)" } }
        }
    }
    # MinGW (PATH)
    $pathGxx = (Get-Command g++.exe -ErrorAction SilentlyContinue).Source
    if ($pathGxx) { return @{ CXX = $pathGxx; CC = (Join-Path (Split-Path $pathGxx) "gcc.exe"); Kind = "G++ (PATH)" } }
    # Clang (PATH)
    $pathClang = (Get-Command clang++.exe -ErrorAction SilentlyContinue).Source
    if ($pathClang) { return @{ CXX = $pathClang; CC = (Join-Path (Split-Path $pathClang) "clang.exe"); Kind = "Clang++" } }
    # MSVC
    $pathCL = (Get-Command cl.exe -ErrorAction SilentlyContinue).Source
    if ($pathCL) { return @{ CXX = $pathCL; CC = $pathCL; Kind = "MSVC (cl.exe)" } }
    return $null
}

# ── Interactive prompts ─────────────────────────────────────────
function Prompt-Bool {
    param([string]$Label, [string]$Default = "N")
    $yn = if ($Default -eq "Y") { "Y/n" } else { "y/N" }
    $reply = Read-Host "$Label ($yn)"
    if ([string]::IsNullOrWhiteSpace($reply)) { $reply = $Default }
    return ($reply -eq "y" -or $reply -eq "Y")
}

# ── Main ────────────────────────────────────────────────────────
Write-Host ""
Write-Cyan "╔══════════════════════════════════════════╗"
Write-Cyan "║     ADOCAO  v3.0.0  —  Build Script      ║"
Write-Cyan "╚══════════════════════════════════════════╝"
Write-Host ""

# ── Determine mode ──────────────────────────────────────────────
$interactive = (-not ($PSBoundParameters.ContainsKey('Portable') -or
                      $PSBoundParameters.ContainsKey('ExZoom')))

# ── Detect compiler ──────────────────────────────────────────────
$compiler = Find-Compiler
if (-not $compiler) {
    Write-Host "ERROR: No compiler found (g++, clang++, or cl.exe)." -ForegroundColor Red
    Write-Host "Install MinGW:  winget install BrechtSanders.WinLibs.POSIX.UCRT.LLVM"
    exit 1
}
Write-Host "Compiler detected: " -NoNewline
Write-Green $compiler.Kind
Write-Dim "  CC  = $($compiler.CC)"
Write-Dim "  CXX = $($compiler.CXX)"

# ── Gather options ───────────────────────────────────────────────
if ($interactive) {
    Write-Host ""
    Write-Host "Build options (press Enter for default):" -ForegroundColor Yellow
    $Portable   = Prompt-Bool "  Static-linked portable build?"  "N"
    $ExZoom     = Prompt-Bool "  Extra zoom (min 0.5x)?"         "N"
}

# ── Generator selection ─────────────────────────────────────────
if (-not $Generator) {
    if ($compiler.Kind -like "*MinGW*" -or $compiler.Kind -like "*G++*" -or $compiler.Kind -like "*Clang*") {
        $Generator = "MinGW Makefiles"
    } elseif ($compiler.Kind -like "*MSVC*") {
        $Generator = "Visual Studio 17 2022"
    } else {
        $Generator = "Ninja"
    }
}

# ── Summary ─────────────────────────────────────────────────────
Write-Host ""
Write-Host "Configuration:" -ForegroundColor Yellow
Write-Host "  Portable:     " -NoNewline; if ($Portable) { Write-Green "ON" } else { Write-Dim "OFF" }
Write-Host "  Extra Zoom:   " -NoNewline; if ($ExZoom)   { Write-Green "ON" } else { Write-Dim "OFF" }
Write-Host "  Generator:    " -NoNewline; Write-Dim $Generator
Write-Host "  Build type:   " -NoNewline; Write-Dim $BuildType
Write-Host ""

# ── CMake configure ─────────────────────────────────────────────
$cmakeArgs = @(
    "-G", $Generator,
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DADOCAO_PORTABLE=$(if ($Portable) { 'ON' } else { 'OFF' })",
    "-DADOCAO_EXTRA_ZOOM=$(if ($ExZoom) { 'ON' } else { 'OFF' })"
)

if ($compiler.Kind -notlike "*MSVC*") {
    $cmakeArgs += "-DCMAKE_C_COMPILER=$($compiler.CC)"
    $cmakeArgs += "-DCMAKE_CXX_COMPILER=$($compiler.CXX)"
}

if (-not (Test-Path build)) { New-Item -ItemType Directory build | Out-Null }
Push-Location build

Write-Cyan "Configuring..."
$cfgLog = "$env:TEMP\adocao-cmake-config.log"
& cmake .. @cmakeArgs 2>&1 | ForEach-Object {
    if ($_ -match "error|Error|ERROR") { Write-Host $_ -ForegroundColor Red }
    $_
} | Out-File $cfgLog -Encoding utf8

if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake configure FAILED. Log: $cfgLog" -ForegroundColor Red
    Pop-Location; exit 1
}
Write-Dim "  OK"

# ── CMake build ─────────────────────────────────────────────────
Write-Cyan "Building..."
$buildArgs = @("--build", ".", "--parallel")
if ($Generator -eq "Ninja" -or $Generator -eq "MinGW Makefiles") {
    # CMake's --parallel handles these correctly
}

# Run build and capture output, showing progress with [XX%] markers
$lastPct = -1
& cmake @buildArgs 2>&1 | ForEach-Object {
    $line = $_
    # Show progress lines ([XX%]) in green
    if ($line -match '\[\s*(\d+)%\]') {
        $pct = [int]$Matches[1]
        if ($pct -ne $lastPct -and $pct % 5 -eq 0) {
            Write-Green $line
        } elseif ($pct -gt $lastPct + 10) {
            Write-Green $line
        }
        $lastPct = $pct
    } elseif ($line -match 'error|Error|ERROR|fatal') {
        Write-Host $line -ForegroundColor Red
    } elseif ($line -match 'warning') {
        Write-Host $line -ForegroundColor Yellow
    } else {
        # Only show non-progress lines selectively to reduce noise
        if ($line -notmatch '^\[.*\] Building|^\[.*\] Linking') {
            Write-Dim $line
        }
    }
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build FAILED." -ForegroundColor Red
    Pop-Location; exit 1
}

Pop-Location

# ── Done ─────────────────────────────────────────────────────────
Write-Host ""
Write-Green "╔══════════════════════════════════════════╗"
Write-Green "║            Build successful!             ║"
Write-Green "╚══════════════════════════════════════════╝"
Write-Host ""
Write-Host "  " -NoNewline
Write-Green (Resolve-Path "build\ADOCAO.exe")
Write-Host ""
