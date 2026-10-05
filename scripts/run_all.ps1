<#
.SYNOPSIS
    Builds QuantForge and checks that everything works, step by step.

.DESCRIPTION
    1. Configure + build the C++ library, tests, and benchmarks
    2. Run the GoogleTest suite
    3. Run both benchmarks
    4. Install the Python package (if it isn't installed yet)
    5. Run the Python demo (metrics table, replay check, chart)
    Prints a PASS/FAIL summary at the end.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\run_all.ps1
    powershell -ExecutionPolicy Bypass -File scripts\run_all.ps1 -ReinstallPython -NoShow
#>
param(
    [switch]$ReinstallPython,  # force a rebuild of the Python extension
    [switch]$NoShow            # don't open the chart at the end
)

$ErrorActionPreference = "Continue"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

# --- Locate the toolchain -------------------------------------------------
$WinLibs = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
if (Test-Path $WinLibs) { $env:Path = "$WinLibs;$env:Path" }

$Python = (Get-Command python -ErrorAction SilentlyContinue).Source
$PyScripts = if ($Python) { Join-Path (Split-Path $Python) "Scripts" } else { $null }
if ($PyScripts -and (Test-Path $PyScripts)) { $env:Path = "$PyScripts;$env:Path" }

$Gcc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
$Gxx = (Get-Command g++ -ErrorAction SilentlyContinue).Source
$Ninja = (Get-Command ninja -ErrorAction SilentlyContinue).Source

$results = [ordered]@{}
function Step($name) { Write-Host ""; Write-Host "=== $name ===" -ForegroundColor Cyan }
function Record($name, $ok) {
    $results[$name] = $ok
    if ($ok) { Write-Host "[PASS] $name" -ForegroundColor Green }
    else { Write-Host "[FAIL] $name" -ForegroundColor Red }
}

Step "Toolchain"
Write-Host "gcc    : $Gcc"
Write-Host "g++    : $Gxx"
Write-Host "ninja  : $Ninja"
Write-Host "python : $Python"
Record "Toolchain found" ([bool]($Gcc -and $Gxx -and $Ninja -and $Python))
if (-not $results["Toolchain found"]) {
    Write-Host "Install GCC 11+, CMake, Ninja, and Python 3.9+ first (see README)." -ForegroundColor Yellow
    exit 1
}
& $Gxx --version | Select-Object -First 1

# --- 1. Build -------------------------------------------------------------
Step "1. Configure + build (Release)"
if (-not (Test-Path "build\build.ninja")) {
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_C_COMPILER=$Gcc" "-DCMAKE_CXX_COMPILER=$Gxx" "-DCMAKE_MAKE_PROGRAM=$Ninja" | Out-Host
}
cmake --build build | Out-Host
Record "C++ build" ($LASTEXITCODE -eq 0)

# --- 2. Tests -------------------------------------------------------------
Step "2. Unit tests (GoogleTest)"
& .\build\bin\quantforge_tests.exe --gtest_brief=1 | Out-Host
Record "Unit tests" ($LASTEXITCODE -eq 0)

# --- 3. Benchmarks --------------------------------------------------------
Step "3. Benchmarks (1,000,000 inputs each)"
& .\build\bin\bench_engine.exe 1000000 | Out-Host
$engineOk = ($LASTEXITCODE -eq 0)
Write-Host ""
& .\build\bin\bench_event_queue.exe 1000000 | Out-Host
Record "Benchmarks" ($engineOk -and $LASTEXITCODE -eq 0)

# --- 4. Python package ----------------------------------------------------
Step "4. Python package"
Remove-Item Env:\PYTHONPATH -ErrorAction SilentlyContinue   # never shadow the installed package
& $Python -c "import quantforge" 2>$null
$installed = ($LASTEXITCODE -eq 0)
if ($ReinstallPython -or -not $installed) {
    Write-Host "Building and installing the quantforge Python package (takes a minute)..."
    & $Python -m pip install --quiet numpy pandas matplotlib scikit-build-core pybind11 | Out-Host
    $env:CMAKE_GENERATOR = "Ninja"; $env:CC = $Gcc; $env:CXX = $Gxx; $env:CMAKE_MAKE_PROGRAM = $Ninja
    & $Python -m pip install . --no-build-isolation --force-reinstall --no-deps --quiet | Out-Host
}
& $Python -c "import quantforge as qf; print('quantforge', qf.__version__, 'from', qf.__file__)"
Record "Python import" ($LASTEXITCODE -eq 0)

# --- 5. Python demo -------------------------------------------------------
Step "5. Python demo (3 strategies + replay check)"
$demoArgs = @("examples\demo.py")
if ($NoShow) { $demoArgs += "--no-show" }
& $Python @demoArgs
Record "Python demo" ($LASTEXITCODE -eq 0)

# --- Summary --------------------------------------------------------------
Step "Summary"
$failed = 0
foreach ($k in $results.Keys) {
    if ($results[$k]) { Write-Host ("  PASS  {0}" -f $k) -ForegroundColor Green }
    else { Write-Host ("  FAIL  {0}" -f $k) -ForegroundColor Red; $failed++ }
}
Write-Host ""
if ($failed -eq 0) { Write-Host "Everything works." -ForegroundColor Green; exit 0 }
else { Write-Host "$failed step(s) failed. Scroll up for details." -ForegroundColor Red; exit 1 }
