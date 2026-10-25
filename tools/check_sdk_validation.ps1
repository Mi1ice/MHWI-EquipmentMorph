param([ValidateSet('Cases','Sdk','Controller','Build','All')][string]$Stage='All',[string]$Python='python',[string]$OutputDirectory)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if(!$OutputDirectory){$OutputDirectory=Join-Path $taskRoot '.build/sdk-validation'}
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
. (Join-Path $PSScriptRoot 'windows-toolchain.ps1')
$taskToolchain=Get-EquipmentMorphToolchain
$taskCompiler=Join-Path $taskToolchain.CompilerRoot 'bin/Hostx64/x64/cl.exe'
$taskIncludes=@((Join-Path $taskToolchain.CompilerRoot 'include'))
foreach($taskPart in @('ucrt','shared','um')){$taskIncludes+=Join-Path $taskToolchain.SdkRoot ('Include/'+$taskToolchain.SdkVersion+'/'+$taskPart)}
$taskLibraries=@((Join-Path $taskToolchain.CompilerRoot 'lib/x64'))
foreach($taskPart in @('ucrt','um')){$taskLibraries+=Join-Path $taskToolchain.SdkRoot ('Lib/'+$taskToolchain.SdkVersion+'/'+$taskPart+'/x64')}
$taskCommon=@('/nologo','/utf-8','/std:c++20','/EHsc','/MT','/O2','/W4','/WX','/fp:strict')+@($taskIncludes | ForEach-Object {'/I'+$_})
function Invoke-ValidationTest([string]$Name){
    $taskSource=Join-Path $taskRoot ('controls/MHWI-EquipmentMorph-SdkValidation/'+$Name+'.cpp')
    if(!(Test-Path -LiteralPath $taskSource)){throw ('Stage capability not implemented: '+$Name)}
    $taskExe=Join-Path $taskOutput ($Name+'.exe')
    & $taskCompiler @taskCommon $taskSource ('/Fo'+(Join-Path $taskOutput ($Name+'.obj'))) ('/Fe'+$taskExe) /link @($taskLibraries | ForEach-Object {'/LIBPATH:'+$_}) kernel32.lib
    if($LASTEXITCODE -ne 0){throw ('Compile failed: '+$Name)}
    & $taskExe
    if($LASTEXITCODE -ne 0){throw ('Checks failed: '+$Name)}
}
if($Stage -in @('Cases','All')){
    Invoke-ValidationTest 'case_tests'
    & $Python (Join-Path $PSScriptRoot 'test_prepare_sdk_validation_catalog.py')
    if($LASTEXITCODE -ne 0){throw 'Catalog preparation checks failed'}
}
if($Stage -in @('Sdk','All')){
    Invoke-ValidationTest 'sdk_session_tests'
    & (Join-Path $PSScriptRoot 'build_mhwi_equipmentmorph_sdk_v04.ps1') -OutputDirectory (Join-Path $taskOutput 'sdk-regression')
    if($LASTEXITCODE -ne 0){throw 'Existing SDK regression failed'}
}
if($Stage -notin @('Cases','Sdk')){throw ('Stage not implemented: '+$Stage)}
