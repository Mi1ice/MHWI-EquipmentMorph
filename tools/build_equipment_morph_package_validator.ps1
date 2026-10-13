param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'windows-toolchain.ps1')
$taskToolchain=Get-EquipmentMorphToolchain
$taskCompilerRoot=$taskToolchain.CompilerRoot
$taskSdkRoot=$taskToolchain.SdkRoot
$taskSdkVersion=$taskToolchain.SdkVersion
$taskSdk=$taskSdkRoot
$taskVersion=$taskSdkVersion
$taskOutput=if($OutputDirectory){[IO.Path]::GetFullPath($OutputDirectory)}else{Join-Path $taskRoot '.build/current-validation/package-validator'}
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskArgs=@('/nologo','/utf-8','/MT','/O2','/std:c++20','/EHsc','/W4','/WX','/fp:strict',('/I'+(Join-Path $taskCompilerRoot 'include')))
foreach($taskComponent in @('ucrt','shared','um')){$taskArgs+='/I'+(Join-Path $taskSdk ('Include/'+$taskVersion+'/'+$taskComponent))}
$taskArgs+=@((Join-Path $taskRoot 'tools/validate_equipment_morph_packages.cpp'),('/Fo'+(Join-Path $taskOutput 'validator.obj')),('/Fe'+(Join-Path $taskOutput 'EquipmentMorphPackageValidator.exe')),'/link','/Brepro',('/LIBPATH:'+(Join-Path $taskCompilerRoot 'lib/x64')))
foreach($taskComponent in @('ucrt','um')){$taskArgs+='/LIBPATH:'+(Join-Path $taskSdk ('Lib/'+$taskVersion+'/'+$taskComponent+'/x64'))}
$taskArgs+=@('kernel32.lib','bcrypt.lib')
& (Join-Path $taskCompilerRoot 'bin/Hostx64/x64/cl.exe') @taskArgs
if($LASTEXITCODE -ne 0){throw 'Package validator build failed'}
