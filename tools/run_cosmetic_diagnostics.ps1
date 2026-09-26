param(
  [Parameter(Mandatory = $true)][string]$Iso,
  [Parameter(Mandatory = $true)][string]$TomNookZip,
  [string]$BuildDir = "build-cosmetic",
  [ValidateSet("Release", "RelWithDebInfo", "Debug")][string]$Configuration = "Release",
  [switch]$LaunchManual
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$IsoPath = (Resolve-Path $Iso).Path
$ZipPath = (Resolve-Path $TomNookZip).Path
$BuildPath = Join-Path $Root $BuildDir
$Exe = Join-Path $BuildPath "port/$Configuration/melee_port.exe"
$DiagnosticRoot = Join-Path $BuildPath "cosmetic-diagnostic"
$Settings = Join-Path $DiagnosticRoot "port-settings.ini"
$ModLog = Join-Path $DiagnosticRoot "mod-on.log"
$VanillaLog = Join-Path $DiagnosticRoot "vanilla.log"

if (-not (Test-Path $Exe)) {
  throw "Build not found at $Exe. Run tools/build_cosmetic_milestone.ps1 first."
}

# melee_port is linked as a Windows GUI executable. Windows PowerShell returns immediately when a
# GUI executable is invoked with &, which races the log checks below while the headless diagnostic
# is still running. Start-Process -Wait gives both Windows PowerShell and PowerShell 7 the same
# synchronous behavior. Quote whitespace-bearing arguments explicitly because -ArgumentList joins
# its array into one native command line.
function Invoke-MeleePort {
  param([Parameter(Mandatory = $true)][string[]]$Arguments)
  $QuotedArguments = @($Arguments | ForEach-Object {
    if ($_ -match '[\s"]') { '"' + $_.Replace('"', '\"') + '"' } else { $_ }
  })
  $Process = Start-Process -FilePath $Exe -ArgumentList $QuotedArguments -NoNewWindow -Wait -PassThru
  return $Process.ExitCode
}

New-Item -ItemType Directory -Force $DiagnosticRoot | Out-Null
Push-Location $Root
try {
  ctest --test-dir $BuildPath -C $Configuration -R "port_cosmetic_(mods|import_diagnostic)" --output-on-failure
  if ($LASTEXITCODE -ne 0) { throw "Native cosmetic unit tests failed." }

  python tools/cosmetic_import_diagnostic.py $ZipPath
  if ($LASTEXITCODE -ne 0) { throw "Tom Nook source validation failed." }

  $ExitCode = Invoke-MeleePort @('--settings-path', $Settings, '--import-cosmetic', $ZipPath, '--cosmetic-status')
  if ($ExitCode -ne 0) { throw "Native Tom Nook import failed." }

  $ExitCode = Invoke-MeleePort @('--iso', $IsoPath, '--headless', '--fast', '--frames', '1400', '--script', 'port/scripts/boot_to_menu.txt', '--volume', '0', '--settings-path', $Settings, '--log-file', $ModLog)
  if ($ExitCode -ne 0) { throw "Mod-enabled boot diagnostic failed." }
  if (-not (Select-String -Quiet -Path $ModLog -Pattern "cosmetics: .* -> PlFxGr.dat")) {
    throw "The mod-enabled boot did not bind Tom Nook to PlFxGr.dat. Inspect $ModLog"
  }

  $ExitCode = Invoke-MeleePort @('--settings-path', $Settings, '--restore-vanilla-cosmetics', '--cosmetic-status')
  if ($ExitCode -ne 0) { throw "Restore Vanilla command failed." }
  $ExitCode = Invoke-MeleePort @('--iso', $IsoPath, '--headless', '--fast', '--frames', '300', '--script', 'port/scripts/boot_only.txt', '--volume', '0', '--settings-path', $Settings, '--log-file', $VanillaLog)
  if ($ExitCode -ne 0) { throw "Vanilla-restored boot diagnostic failed." }
  if (-not (Select-String -Quiet -Path $VanillaLog -Pattern "cosmetics: vanilla profile")) {
    throw "The restored boot did not report the vanilla profile. Inspect $VanillaLog"
  }

  # Leave the isolated diagnostic profile ready for the manual green-Fox rendering check.
  $ExitCode = Invoke-MeleePort @('--settings-path', $Settings, '--import-cosmetic', $ZipPath)
  if ($ExitCode -ne 0) { throw "Could not restage Tom Nook for the manual check." }

  Write-Host "PASS: source validation, native import, profile persistence, mod-on FST binding, and Restore Vanilla boot."
  Write-Host "Manual check still required: launch the command below, select green Fox, and verify the model/portrait behavior in game."
  Write-Host "`"$Exe`" --iso `"$IsoPath`" --threaded-renderer --fps 120 --frame-mode authored --settings-path `"$Settings`""
  Write-Host "Stock-peer Direct, replay playback, and frame-time checks remain separate manual validation gates."

  if ($LaunchManual) {
    $ExitCode = Invoke-MeleePort @('--iso', $IsoPath, '--threaded-renderer', '--fps', '120', '--frame-mode', 'authored', '--settings-path', $Settings)
    if ($ExitCode -ne 0) { throw "Manual cosmetic launch failed." }
  }
}
finally {
  Pop-Location
}
