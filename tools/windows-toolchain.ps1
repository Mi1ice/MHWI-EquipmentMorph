function Get-EquipmentMorphToolchain {
    $taskCompilerRoot = $env:MHWI_MSVC_ROOT
    if (-not $taskCompilerRoot) {
        $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (-not (Test-Path -LiteralPath $taskVswhere)) { throw 'Install Visual Studio C++ Build Tools, or set MHWI_MSVC_ROOT.' }
        $taskVsRoot = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -ne 0 -or -not $taskVsRoot) { throw 'No Visual Studio C++ toolchain found.' }
        $taskCompilerRoot = (Get-ChildItem -LiteralPath (Join-Path $taskVsRoot 'VC/Tools/MSVC') -Directory |
            Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).FullName
    }
    $taskSdkRoot = $env:MHWI_WINDOWS_SDK_ROOT
    if (-not $taskSdkRoot) { $taskSdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10' }
    $taskSdkVersion = $env:MHWI_WINDOWS_SDK_VERSION
    if (-not $taskSdkVersion) {
        $taskSdkVersion = (Get-ChildItem -LiteralPath (Join-Path $taskSdkRoot 'Include') -Directory |
            Where-Object { (Test-Path -LiteralPath (Join-Path $_.FullName 'um/Windows.h')) -and
                (Test-Path -LiteralPath (Join-Path $taskSdkRoot ('Lib/' + $_.Name + '/um/x64/kernel32.lib'))) } |
            Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
    }
    if (-not $taskSdkVersion -or -not (Test-Path -LiteralPath (Join-Path $taskCompilerRoot 'bin/Hostx64/x64/cl.exe'))) {
        throw 'MSVC x64 compiler or Windows SDK missing.'
    }
    return @{ CompilerRoot = $taskCompilerRoot; SdkRoot = $taskSdkRoot; SdkVersion = $taskSdkVersion }
}
