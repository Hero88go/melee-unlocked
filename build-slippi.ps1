$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $env:DXSDK_DIR = (Resolve-Path 'dependencies/dxsdk').Path + '/'
    & 'C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/MSBuild/Current/Bin/MSBuild.exe' `
        slippi/Source/Dolphin.sln /p:Configuration=Release /p:Platform=x64 /m:2 /nologo /verbosity:minimal
    if ($LASTEXITCODE -ne 0) { throw 'Slippi build failed' }
    python tools/package_prototype.py
    if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' }
} finally { Pop-Location }
