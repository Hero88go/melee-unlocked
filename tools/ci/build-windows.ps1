# Windows build of the pushed commit for the rolling "Latest" prerelease (.github/workflows/latest-prerelease.yml).
# Builds the Source Port (melee_source.exe + melee_game.dll), its SSE2 compatibility build and the launcher, then lays
# them over the newest published release zip (tools/ci/overlay-release.py). MELEE_SOURCE_ONLY=ON: no disc-derived
# translation is needed, only the two fact headers in tools/ci/source-only/. The Static Recomp (melee_port.exe) is not
# built here. Every option is the repository default (production YouGame, no test hooks), as in a real release.
param([Parameter(Mandatory)][ValidateSet('validate','inputs','toolchain','dll','host','package','checks')][string]$Step)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$Repo = (Resolve-Path "$PSScriptRoot/../..").Path
$Work = Join-Path $env:RUNNER_TEMP 'mu-ci'
$Logs = Join-Path $Work 'logs'
$Runtime = Join-Path $Work 'runtime'
$Mingw = Join-Path $env:RUNNER_TEMP 'winlibs/mingw64'
$Gen = Join-Path $Repo 'tools/ci/source-only'
$DllBuild = Join-Path $Work 'build-game'
$CompatDllBuild = Join-Path $Work 'build-game-compat'
$HostBuild = Join-Path $Work 'build-host'
$CompatHostBuild = Join-Path $Work 'build-host-compat'
$Out = Join-Path $Work 'out'
$Par = if ($env:NUMBER_OF_PROCESSORS) { $env:NUMBER_OF_PROCESSORS } else { '4' }
# The options every host tree is configured with. Nothing YouGame-test-only: YOUGAME_AGENT_TEST_HOOK and
# MELEE_YOUGAME_START_STAGING keep their OFF defaults and are checked in the CMake cache after configuring.
$HostOptions = @('-DMELEE_BUILD_EXPERIMENTAL_PORT=ON','-DMELEE_SOURCE_ONLY=ON',"-DPORT_GEN=$Gen",'-DMELEE_REQUIRE_STREAMLINE=ON')
New-Item -ItemType Directory -Force $Logs | Out-Null

function Native([string]$Log, [string]$Program, [string[]]$Arguments) {
    & $Program @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $Logs $Log) -Append
    if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE); see $Log" }
}
function RequireFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Required file missing: $Path" }
}
function RequireReleaseCache([string]$Tree) {
    $Cache = Get-Content -LiteralPath "$Tree/CMakeCache.txt"
    foreach ($Want in @('YOUGAME_AGENT_TEST_HOOK:BOOL=OFF','MELEE_YOUGAME_START_STAGING:BOOL=OFF','MELEE_SOURCE_ONLY:BOOL=ON')) {
        if ($Cache -cnotcontains $Want) { throw "$Tree is not a release configuration: expected $Want" }
    }
}

Push-Location $Repo
Start-Transcript -Path (Join-Path $Logs "$Step.log") -Append | Out-Null
try {
    switch ($Step) {
        validate {
            $Version = (Get-Content VERSION -Raw).Trim()
            if ($Version -cnotmatch '^[0-9]+(\.[0-9]+){1,3}$') { throw "Unexpected VERSION '$Version'" }
            if ($env:BASE_VERSION -cnotmatch '^[0-9]+(\.[0-9]+){1,3}$') { throw 'Invalid base release version' }
            if ($env:BASE_ZIP_SHA256 -notmatch '^[a-fA-F0-9]{64}$') { throw 'Invalid base release digest' }
            if ($env:WINLIBS_SHA256 -notmatch '^[a-fA-F0-9]{64}$') { throw 'Invalid WinLibs digest' }
            $Headers = @(Get-ChildItem $Gen -File | ForEach-Object Name | Sort-Object)
            if (($Headers -join ',') -cne 'guest_symbols.h,hle_decls.h') { throw 'tools/ci/source-only must hold exactly guest_symbols.h and hle_decls.h' }
            if (-not (Select-String -LiteralPath port/CMakeLists.txt -SimpleMatch 'option(MELEE_SOURCE_ONLY' -Quiet)) { throw 'port/CMakeLists.txt has no MELEE_SOURCE_ONLY option' }
            "VERSION $Version, base release v$($env:BASE_VERSION)" | Tee-Object -FilePath "$Logs/versions.log" -Append
        }
        inputs {
            # The only initialized submodule is public doldecomp/melee, at the gitlink pin.
            Native submodule.log git @('submodule','update','--init','--','sourceport/extern/melee')
            # The newest published release: the base the built files are laid over, and the NVIDIA Streamline DLLs the
            # host build links against. Digest from GitHub's asset listing, checked after the download.
            $env:BASE_ZIP_URL = "https://github.com/$($env:BASE_REPO)/releases/download/v$($env:BASE_VERSION)/MeleeUnlocked-$($env:BASE_VERSION)-win64.zip"
            Native downloads.log python @('tools/ci/inputs.py','download','BASE_ZIP_URL','BASE_ZIP_SHA256',"$Work/base.zip")
            if (Test-Path $Runtime) { Remove-Item -Recurse -Force $Runtime }
            Native extract.log python @('tools/ci/inputs.py','extract-runtime',"$Work/base.zip",$Runtime)
        }
        toolchain {
            # Pinned UCRT/POSIX/SEH x86-64 WinLibs release, digest-checked (WINLIBS_SHA256 in the workflow).
            $env:WINLIBS_URL = 'https://github.com/brechtsanders/winlibs_mingw/releases/download/15.2.0posix-13.0.0-ucrt-r3/winlibs-x86_64-posix-seh-gcc-15.2.0-mingw-w64ucrt-13.0.0-r3.7z'
            $Archive = Join-Path $env:RUNNER_TEMP 'winlibs-download/winlibs.7z'
            Native winlibs-download.log python @('tools/ci/inputs.py','download','WINLIBS_URL','WINLIBS_SHA256',$Archive)
            Native winlibs-extract.log 7z @('x',$Archive,"-o$($env:RUNNER_TEMP)/winlibs",'-y')
            foreach ($Name in @('gcc.exe','g++.exe','ninja.exe')) { RequireFile "$Mingw/bin/$Name" }
            Native versions.log "$Mingw/bin/gcc.exe" @('--version')
            Native versions.log cmake @('--version')
            Native versions.log python @('--version')
            # The newest Visual Studio with the x64 C++ tools; the matching CMake generator is pointed at it.
            $Vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
            $Vs = & $Vswhere -latest -version '[17.0,' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($LASTEXITCODE -ne 0 -or -not $Vs) { throw 'Visual Studio (2022 or newer) x64 C++ tools missing from runner' }
            $VsMajor = ((& $Vswhere -latest -version '[17.0,' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion) -split '\.')[0]
            $VsGen = switch ($VsMajor) { '17' { 'Visual Studio 17 2022' } '18' { 'Visual Studio 18 2026' } default { throw "Unsupported Visual Studio major version $VsMajor" } }
            Set-Content -LiteralPath "$Work/vs.txt" -Value $Vs
            Set-Content -LiteralPath "$Work/vs-generator.txt" -Value $VsGen
            "Visual Studio: $Vs ($VsGen)" | Add-Content "$Logs/versions.log"
        }
        dll {
            Native prepare.log python @('tools/prepare_native_sources.py')
            $OldPath = $env:PATH
            try {
                $env:PATH = "$Mingw/bin;$OldPath"
                foreach ($Tree in @(@{ Dir = $DllBuild; Extra = @() }, @{ Dir = $CompatDllBuild; Extra = @('-DMELEE_CPU_BASELINE=SSE2') })) {
                    $Name = Split-Path $Tree.Dir -Leaf
                    Native "$Name-configure.log" cmake (@('-S','sourceport/game','-B',$Tree.Dir,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
                        "-DCMAKE_TOOLCHAIN_FILE=$Repo/sourceport/cmake/mingw-w64-x86_64.cmake","-DMELEE_MINGW_ROOT=$Mingw") + $Tree.Extra)
                    Native "$Name.log" cmake @('--build',$Tree.Dir,'--target','melee_game','--parallel',$Par)
                }
            } finally { $env:PATH = $OldPath }
        }
        host {
            $Vs = (Get-Content "$Work/vs.txt" -Raw).Trim()
            $VsGen = (Get-Content "$Work/vs-generator.txt" -Raw).Trim()
            & "$Vs/Common7/Tools/Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
            (Get-Item (Get-Command cl.exe).Source).VersionInfo.FileVersion | Add-Content "$Logs/versions.log"
            $env:CL = '/MP'   # one project, many files: MSBuild's --parallel only runs separate projects at once
            $Sl = Join-Path $Repo 'port/third_party/streamline/bin/x64'
            New-Item -ItemType Directory -Force $Sl | Out-Null
            foreach ($Dll in @('sl.interposer.dll','sl.common.dll','sl.dlss.dll','nvngx_dlss.dll','sl.dlss_d.dll','nvngx_dlssd.dll','sl.dlss_g.dll','nvngx_dlssg.dll','sl.reflex.dll','sl.pcl.dll')) {
                if (Test-Path "$Runtime/$Dll") { Copy-Item "$Runtime/$Dll" $Sl -Force }
            }
            foreach ($Need in @('port/third_party/streamline/include/sl.h','port/third_party/streamline/lib/x64/sl.interposer.lib',"$Sl/sl.interposer.dll","$Sl/sl.common.dll")) { RequireFile $Need }
            Native host-configure.log cmake (@('-S','.','-B',$HostBuild,'-G',$VsGen,'-A','x64',"-DCMAKE_GENERATOR_INSTANCE=$Vs") + $HostOptions)
            RequireReleaseCache $HostBuild
            Native host-build.log cmake @('--build',$HostBuild,'--config','Release','--target','melee_source','--parallel',$Par)
            Native launcher-build.log cmake @('--build',$HostBuild,'--config','Release','--target','melee_unlocked','--parallel',$Par)
            RequireFile "$HostBuild/port/Release/MeleeUnlockedLauncher.exe"
            # Separate SSE2 tree: root CMake also applies the baseline to native_animation.
            Native host-compat-configure.log cmake (@('-S','.','-B',$CompatHostBuild,'-G',$VsGen,'-A','x64',"-DCMAKE_GENERATOR_INSTANCE=$Vs",'-DMELEE_CPU_BASELINE=SSE2') + $HostOptions)
            RequireReleaseCache $CompatHostBuild
            Native host-compat-build.log cmake @('--build',$CompatHostBuild,'--config','Release','--target','melee_source','--parallel',$Par)
        }
        package {
            $HostOut = "$HostBuild/port/Release"; $CompatOut = "$CompatHostBuild/port/Release"
            foreach ($Pair in @(@($HostOut, $DllBuild, 'audit'), @($CompatOut, $CompatDllBuild, 'audit-compat'))) {
                Native "$($Pair[2]).log" python @('tools/audit_source_provenance.py','--host-map',"$($Pair[0])/melee_source.map",
                    '--game-map',"$($Pair[1])/melee_game.map",'--sources',"$($Pair[1])/native-game-sources.txt",
                    '--host-exe',"$($Pair[0])/melee_source.exe",'--game-dll',"$($Pair[1])/melee_game.dll",'--out',"$Logs/$($Pair[2]).json")
            }
            $Built = Join-Path $Work 'built'
            if (Test-Path $Built) { Remove-Item -Recurse -Force $Built }
            New-Item -ItemType Directory -Force $Built | Out-Null
            $Files = [ordered]@{
                'melee_source.exe' = "$HostOut/melee_source.exe"
                'melee_game.dll' = "$DllBuild/melee_game.dll"
                'melee_game.snapexcl' = "$DllBuild/melee_game.snapexcl"
                'MeleeUnlockedLauncher.exe' = "$HostOut/MeleeUnlockedLauncher.exe"
                'rtx_transform.dxil' = "$HostOut/rtx_transform.dxil"
                'rtx_trace.dxil' = "$HostOut/rtx_trace.dxil"
                'rtx_direct.dxil' = "$HostOut/rtx_direct.dxil"
                'melee_source_compat.exe' = "$CompatOut/melee_source.exe"
                'melee_game_compat.dll' = "$CompatDllBuild/melee_game.dll"
                'melee_game_compat.snapexcl' = "$CompatDllBuild/melee_game.snapexcl"
            }
            foreach ($Name in $Files.Keys) { RequireFile $Files[$Name]; Copy-Item -LiteralPath $Files[$Name] -Destination "$Built/$Name" }
            New-Item -ItemType Directory -Force $Out | Out-Null
            Native overlay.log python @('tools/ci/overlay-release.py','--base',"$Work/base.zip",'--base-sha256',$env:BASE_ZIP_SHA256,
                '--base-version',$env:BASE_VERSION,'--built',$Built,'--repo',$Repo,'--commit',$env:GITHUB_SHA,'--source-url',"$($env:GITHUB_SERVER_URL)/$($env:GITHUB_REPOSITORY)",
                '--out',"$Out/MeleeUnlocked-latest-win64.zip")
        }
        checks {
            Native verify-package.log python @('tools/ci/verify-package.py',"$Out/MeleeUnlocked-latest-win64.zip",'--repo',$Repo)
            $Hash = (Get-FileHash "$Out/MeleeUnlocked-latest-win64.zip" -Algorithm SHA256).Hash.ToLower()
            [IO.File]::WriteAllText("$Out/MeleeUnlocked-latest-win64.zip.sha256", "$Hash  MeleeUnlocked-latest-win64.zip`n", [Text.UTF8Encoding]::new($false))
            "zip_sha256=$Hash" >> $env:GITHUB_OUTPUT
        }
    }
} finally {
    Stop-Transcript | Out-Null
    Pop-Location
}
