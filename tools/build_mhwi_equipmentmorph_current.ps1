$ErrorActionPreference = 'Stop'
$taskWorkspace = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'windows-toolchain.ps1')
$taskToolchain=Get-EquipmentMorphToolchain
$taskCompilerRoot=$taskToolchain.CompilerRoot
$taskSdkRoot=$taskToolchain.SdkRoot
$taskSdkVersion=$taskToolchain.SdkVersion

$taskDependencyDir = Join-Path $taskWorkspace 'research/safetyhook-local-v0.7.0'
$taskSourceDir = Join-Path $taskWorkspace 'diagnostics/EquipmentMorphDiskContract'
$taskOutputDir = Join-Path $taskWorkspace '.build/cleanup-validation/native'
New-Item -ItemType Directory -Path $taskOutputDir -Force | Out-Null
$taskIncludes = @((Join-Path $taskCompilerRoot 'include'), $taskDependencyDir)
foreach ($taskPart in @('ucrt','shared','um')) { $taskIncludes += Join-Path $taskSdkRoot ('Include/' + $taskSdkVersion + '/' + $taskPart) }
$taskLibraries = @((Join-Path $taskCompilerRoot 'lib/x64'))
foreach ($taskPart in @('ucrt','um')) { $taskLibraries += Join-Path $taskSdkRoot ('Lib/' + $taskSdkVersion + '/' + $taskPart + '/x64') }
$taskCompiler = Join-Path $taskCompilerRoot 'bin/Hostx64/x64/cl.exe'
$taskLinker = Join-Path $taskCompilerRoot 'bin/Hostx64/x64/link.exe'
$taskCommon = @('/nologo','/utf-8','/MT','/O2','/DZYDIS_STATIC_BUILD','/DZYCORE_STATIC_BUILD')
$taskCommon += $taskIncludes | ForEach-Object { '/I' + $_ }
$taskArgs = $taskCommon + @('/TC','/W3','/c',(Join-Path $taskDependencyDir 'Zydis.c'),('/Fo' + (Join-Path $taskOutputDir 'Zydis.obj')))
& $taskCompiler @taskArgs
if ($LASTEXITCODE -ne 0) { throw 'Zydis compilation failed' }
$taskArgs = $taskCommon + @('/std:c++latest','/EHsc','/W4','/c',(Join-Path $taskDependencyDir 'safetyhook.cpp'),('/Fo' + (Join-Path $taskOutputDir 'safetyhook.obj')))
& $taskCompiler @taskArgs
if ($LASTEXITCODE -ne 0) { throw 'SafetyHook compilation failed' }
foreach ($taskTarget in @('dll','tests')) {
    $taskSource = if ($taskTarget -eq 'dll') { 'prototype.cpp' } else { 'tests.cpp' }
    $taskName = if ($taskTarget -eq 'dll') { 'MHWEquipmentMorphConfiguredPrototype.dll' } else { 'EquipmentMorphDiskContractTests.exe' }
    $taskArgs = $taskCommon + @('/std:c++latest','/EHsc','/W4','/WX','/fp:strict','/c',(Join-Path $taskSourceDir $taskSource),('/Fo' + (Join-Path $taskOutputDir ($taskTarget + '.obj'))))
    & $taskCompiler @taskArgs
    if ($LASTEXITCODE -ne 0) { throw ('Candidate compilation failed: ' + $taskTarget) }
    $taskLinkArgs = @('/NOLOGO','/MACHINE:X64','/Brepro',('/OUT:"' + (Join-Path $taskOutputDir $taskName) + '"'))
    foreach ($taskObject in @($taskTarget,'safetyhook','Zydis')) { $taskLinkArgs += '"' + (Join-Path $taskOutputDir ($taskObject + '.obj')) + '"' }
    $taskLinkArgs += $taskLibraries | ForEach-Object { '/LIBPATH:"' + $_ + '"' }
    $taskLinkArgs += @('kernel32.lib','user32.lib','bcrypt.lib')
    if ($taskTarget -eq 'dll') { $taskLinkArgs += @('/DLL',('/IMPLIB:"' + (Join-Path $taskOutputDir 'MHWEquipmentMorphConfiguredPrototype.lib') + '"')) }
    $taskResponse = Join-Path $taskOutputDir ($taskTarget + '-link.rsp')
    [System.IO.File]::WriteAllLines($taskResponse,$taskLinkArgs,[System.Text.Encoding]::Unicode)
    & $taskLinker ('@' + $taskResponse)
    if ($LASTEXITCODE -ne 0) { throw ('Candidate link failed: ' + $taskTarget) }
}
