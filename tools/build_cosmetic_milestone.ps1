param(
  [Parameter(Mandatory = $true)][string]$Iso,
  [string]$BuildDir = "build-cosmetic",
  [ValidateSet("Release", "RelWithDebInfo", "Debug")][string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$IsoPath = (Resolve-Path $Iso).Path
$BuildPath = Join-Path $Root $BuildDir
$DolPath = Join-Path $BuildPath "main.dol"

if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
  throw "Python is missing. Run play.bat once or install the repository's documented Windows prerequisites."
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
  throw "CMake is missing. Run play.bat once or install the repository's documented Windows prerequisites."
}

New-Item -ItemType Directory -Force $BuildPath | Out-Null
Push-Location $Root
try {
  python tools/extract_dol.py $IsoPath $DolPath
  if ($LASTEXITCODE -ne 0) { throw "DOL extraction failed. Confirm the ISO is clean Melee NTSC 1.02 (GALE01)." }

  python port/recomp/recomp.py --dol $DolPath --gct-base 0x8065CC80
  if ($LASTEXITCODE -ne 0) { throw "Static recompilation failed." }

  cmake -S . -B $BuildPath -G "Visual Studio 17 2022" -A x64 -DMELEE_BUILD_EXPERIMENTAL_PORT=ON
  if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

  cmake --build $BuildPath --config $Configuration --target melee_port port_cosmetic_mods_test --parallel
  if ($LASTEXITCODE -ne 0) { throw "Windows build failed." }

  $Exe = Join-Path $BuildPath "port/$Configuration/melee_port.exe"
  Write-Host "Built: $Exe"
  Write-Host "Next: powershell -ExecutionPolicy Bypass -File tools/run_cosmetic_diagnostics.ps1 -Iso `"$IsoPath`" -TomNookZip <path-to-Tom-Nook.zip> -BuildDir `"$BuildDir`""
}
finally {
  Pop-Location
}
