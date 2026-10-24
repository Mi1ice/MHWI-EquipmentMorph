param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'windows-toolchain.ps1')
$taskToolchain=Get-EquipmentMorphToolchain
$taskCompilerRoot=$taskToolchain.CompilerRoot
$taskSdkRoot=$taskToolchain.SdkRoot
$taskSdkVersion=$taskToolchain.SdkVersion

$taskSource=Join-Path $taskRoot 'sdk/MHWI-EquipmentMorph-0.4.0';$taskOutput=Join-Path $taskSource 'build'
if($OutputDirectory){$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)}
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskIncludes=@((Join-Path $taskCompilerRoot 'include'),(Join-Path $taskSource 'include'))
foreach($taskPart in @('ucrt','shared','um')){$taskIncludes+=Join-Path $taskSdkRoot ('Include/'+$taskSdkVersion+'/'+$taskPart)}
$taskLibraries=@((Join-Path $taskCompilerRoot 'lib/x64'))
foreach($taskPart in @('ucrt','um')){$taskLibraries+=Join-Path $taskSdkRoot ('Lib/'+$taskSdkVersion+'/'+$taskPart+'/x64')}
$taskCompiler=Join-Path $taskCompilerRoot 'bin/Hostx64/x64/cl.exe';$taskLinker=Join-Path $taskCompilerRoot 'bin/Hostx64/x64/link.exe'
$taskCommon=@('/nologo','/utf-8','/std:c++20','/EHsc','/MT','/O2','/W4','/WX','/fp:strict')+@($taskIncludes|ForEach-Object{'/I'+$_})
foreach($taskInput in @('examples/usage.cpp','tests.cpp')){
    $taskName=[IO.Path]::GetFileNameWithoutExtension($taskInput)
    $taskArgs=$taskCommon+@('/c',(Join-Path $taskSource $taskInput),('/Fo'+(Join-Path $taskOutput ($taskName+'.obj'))))
    & $taskCompiler @taskArgs
    if($LASTEXITCODE -ne 0){throw 'SDK example compilation failed'}
}
$taskLinkArgs=@('/NOLOGO','/MACHINE:X64',('/OUT:"'+(Join-Path $taskOutput 'EquipmentMorphGenericSdkTests.exe')+'"'),('"'+(Join-Path $taskOutput 'tests.obj')+'"'))
$taskLinkArgs+=@($taskLibraries|ForEach-Object{'/LIBPATH:"'+$_+'"'});$taskLinkArgs+='kernel32.lib'
$taskResponse=Join-Path $taskOutput 'tests-link.rsp';[IO.File]::WriteAllLines($taskResponse,$taskLinkArgs,[Text.Encoding]::Unicode)
& $taskLinker ('@'+$taskResponse)
if($LASTEXITCODE -ne 0){throw 'SDK test link failed'}
& (Join-Path $taskOutput 'EquipmentMorphGenericSdkTests.exe')
if($LASTEXITCODE -ne 0){throw 'SDK example checks failed'}
