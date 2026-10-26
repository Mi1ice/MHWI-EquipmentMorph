param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'windows-toolchain.ps1')
$taskChain=Get-EquipmentMorphToolchain
if(-not $OutputDirectory){$OutputDirectory=Join-Path $taskRoot '.build/unified-formats/native'}
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskArgs=@('/nologo','/utf-8','/MT','/O2','/std:c++20','/EHsc','/W4','/WX','/fp:strict',('/I'+(Join-Path $taskChain.CompilerRoot 'include')))
foreach($taskPart in @('ucrt','shared','um')){$taskArgs+='/I'+(Join-Path $taskChain.SdkRoot ('Include/'+$taskChain.SdkVersion+'/'+$taskPart))}
$taskArgs+=@((Join-Path $taskRoot 'tools/test_vertex_declarations.cpp'),('/Fo'+(Join-Path $taskOutput 'declarations.obj')),('/Fe'+(Join-Path $taskOutput 'VertexDeclarationsTests.exe')),'/link','/Brepro',('/LIBPATH:'+(Join-Path $taskChain.CompilerRoot 'lib/x64')))
foreach($taskPart in @('ucrt','um')){$taskArgs+='/LIBPATH:'+(Join-Path $taskChain.SdkRoot ('Lib/'+$taskChain.SdkVersion+'/'+$taskPart+'/x64'))}
& (Join-Path $taskChain.CompilerRoot 'bin/Hostx64/x64/cl.exe') @taskArgs
if($LASTEXITCODE -ne 0){throw 'Declaration test build failed'}
& (Join-Path $taskOutput 'VertexDeclarationsTests.exe')
if($LASTEXITCODE -ne 0){throw 'Declaration tests failed'}
