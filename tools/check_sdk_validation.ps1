param([ValidateSet('Cases','Sdk','Controller','Build','All')][string]$Stage='All',[string]$Python='python',[string]$OutputDirectory)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if(!$OutputDirectory){$OutputDirectory=Join-Path $taskRoot '.build/sdk-validation'}
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskReportPath=Join-Path $taskOutput ($Stage.ToLowerInvariant()+'-report.json')
@{schema='mhwi-sdk-validation-build-v1';stage=$Stage;software_checks_passed=$false;reason='in_progress_or_failed'} | ConvertTo-Json | Set-Content -LiteralPath $taskReportPath -Encoding UTF8
if($Stage -eq 'Build'){@{schema='mhwi-sdk-validation-build-v1';stage='All';software_checks_passed=$false;reason='run_All_for_candidate'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskOutput 'all-report.json') -Encoding UTF8}
. (Join-Path $PSScriptRoot 'windows-toolchain.ps1')
$taskToolchain=Get-EquipmentMorphToolchain
$taskCompiler=Join-Path $taskToolchain.CompilerRoot 'bin/Hostx64/x64/cl.exe'
$taskIncludes=@((Join-Path $taskToolchain.CompilerRoot 'include'))
foreach($taskPart in @('ucrt','shared','um')){$taskIncludes+=Join-Path $taskToolchain.SdkRoot ('Include/'+$taskToolchain.SdkVersion+'/'+$taskPart)}
$taskLibraries=@((Join-Path $taskToolchain.CompilerRoot 'lib/x64'))
foreach($taskPart in @('ucrt','um')){$taskLibraries+=Join-Path $taskToolchain.SdkRoot ('Lib/'+$taskToolchain.SdkVersion+'/'+$taskPart+'/x64')}
$taskCommon=@('/nologo','/utf-8','/std:c++20','/EHsc','/MT','/O2','/W4','/WX','/fp:strict')+@($taskIncludes | ForEach-Object {'/I'+$_})
$taskResults=[ordered]@{}
function Invoke-ValidationTest([string]$Name){
    $taskSource=Join-Path $taskRoot ('controls/MHWI-EquipmentMorph-SdkValidation/'+$Name+'.cpp')
    if(!(Test-Path -LiteralPath $taskSource)){throw ('Stage capability not implemented: '+$Name)}
    $taskExe=Join-Path $taskOutput ($Name+'.exe')
    & $taskCompiler @taskCommon $taskSource ('/Fo'+(Join-Path $taskOutput ($Name+'.obj'))) ('/Fe'+$taskExe) /link @($taskLibraries | ForEach-Object {'/LIBPATH:'+$_}) kernel32.lib
    if($LASTEXITCODE -ne 0){throw ('Compile failed: '+$Name)}
    Push-Location $taskOutput
    try{$taskLines=@(& $taskExe);$taskExit=$LASTEXITCODE}finally{Pop-Location}
    $taskLines | Write-Output
    if($taskExit -ne 0){throw ('Checks failed: '+$Name)}
    $taskMatch=[regex]::Match(($taskLines -join "`n"),'PASS: (\d+) ')
    if(!$taskMatch.Success){throw ('Missing assertion count: '+$Name)}
    $taskResults[$Name]=[ordered]@{passed=$true;checks=[int]$taskMatch.Groups[1].Value}
}
if($Stage -in @('Cases','Build','All')){
    Invoke-ValidationTest 'case_tests'
    $taskPythonLines=@(& $Python (Join-Path $PSScriptRoot 'test_prepare_sdk_validation_catalog.py') 2>&1)
    $taskPythonExit=$LASTEXITCODE;$taskPythonLines | Write-Output
    if($taskPythonExit -ne 0){throw 'Catalog preparation checks failed'}
    $taskMatch=[regex]::Match(($taskPythonLines -join "`n"),'Ran (\d+) tests')
    if(!$taskMatch.Success){throw 'Missing Python test count'}
    $taskResults['catalog_conversion']=[ordered]@{passed=$true;checks=[int]$taskMatch.Groups[1].Value}
}
if($Stage -in @('Sdk','Build','All')){
    Invoke-ValidationTest 'sdk_session_tests'
    $taskSdkLines=@(& (Join-Path $PSScriptRoot 'build_mhwi_equipmentmorph_sdk_v04.ps1') -OutputDirectory (Join-Path $taskOutput 'sdk-regression'))
    $taskSdkExit=$LASTEXITCODE;$taskSdkLines | Write-Output
    if($taskSdkExit -ne 0){throw 'Existing SDK regression failed'}
    $taskGroups=[regex]::Match(($taskSdkLines -join "`n"),'SDK capacity cases: (\d+)/4')
    $taskGeneric=[regex]::Match(($taskSdkLines -join "`n"),'PASS: (\d+) generic SDK checks')
    if(!$taskGroups.Success -or $taskGroups.Groups[1].Value -ne '4' -or !$taskGeneric.Success){throw 'Missing existing SDK results'}
    $taskResults['sdk_regression']=[ordered]@{passed=$true;capacity_groups=[int]$taskGroups.Groups[1].Value;checks=[int]$taskGeneric.Groups[1].Value}
}
if($Stage -in @('Controller','Build','All')){Invoke-ValidationTest 'controller_tests';Invoke-ValidationTest 'window_model_tests'}
if($Stage -in @('Build','All')){
    $taskDll=Join-Path $taskOutput 'MHWEquipmentMorphSdkValidation.dll'
    $taskObjects=@()
    foreach($taskFile in @('window','plugin')){
        $taskObject=Join-Path $taskOutput ($taskFile+'.obj');$taskObjects+=$taskObject
        & $taskCompiler @taskCommon '/DUNICODE' '/D_UNICODE' '/c' (Join-Path $taskRoot ('controls/MHWI-EquipmentMorph-SdkValidation/'+$taskFile+'.cpp')) ('/Fo'+$taskObject)
        if($LASTEXITCODE -ne 0){throw ('Plugin compilation failed: '+$taskFile)}
    }
    $taskLinker=Join-Path $taskToolchain.CompilerRoot 'bin/Hostx64/x64/link.exe'
    $taskLinkArgs=@('/NOLOGO','/DLL','/MACHINE:X64','/INCREMENTAL:NO','/Brepro',('/OUT:"'+$taskDll+'"'))
    $taskLinkArgs+=@($taskObjects|ForEach-Object{'"'+$_+'"'})+@($taskLibraries|ForEach-Object{'/LIBPATH:"'+$_+'"'})+@('kernel32.lib','user32.lib','gdi32.lib')
    $taskResponse=Join-Path $taskOutput 'plugin-link.rsp';[IO.File]::WriteAllLines($taskResponse,$taskLinkArgs,[Text.Encoding]::Unicode)
    & $taskLinker ('@'+$taskResponse)
    if($LASTEXITCODE -ne 0){throw 'Plugin link failed'}
    $taskResults['dll_build']=[ordered]@{passed=$true;loaded=$false;window_created=$false}
}
$taskSources=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
function Add-ValidationSource([string]$Path){
    $taskPath=[IO.Path]::GetFullPath($Path)
    if(!([IO.Path]::GetRelativePath($taskRoot,$taskPath).StartsWith('..')) -and (Test-Path -LiteralPath $taskPath -PathType Leaf) -and $taskSources.Add($taskPath)){
        foreach($taskLine in [IO.File]::ReadAllLines($taskPath)){
            if($taskLine -match '^\s*#\s*include\s+"([^"]+)"'){Add-ValidationSource (Join-Path (Split-Path -Parent $taskPath) $Matches[1])}
        }
    }
}
Get-ChildItem -LiteralPath (Join-Path $taskRoot 'controls/MHWI-EquipmentMorph-SdkValidation') -File | Where-Object {$_.Extension -in @('.hpp','.cpp')} | ForEach-Object {Add-ValidationSource $_.FullName}
foreach($taskName in @('check_sdk_validation.ps1','windows-toolchain.ps1','build_mhwi_equipmentmorph_sdk_v04.ps1','prepare_sdk_validation_catalog.py','test_prepare_sdk_validation_catalog.py','package_sdk_validation.py','test_package_sdk_validation.py')){Add-ValidationSource (Join-Path $PSScriptRoot $taskName)}
foreach($taskName in @('tests.cpp','examples/usage.cpp')){Add-ValidationSource (Join-Path $taskRoot ('sdk/MHWI-EquipmentMorph-0.4.0/'+$taskName))}
Add-ValidationSource (Join-Path $taskRoot 'docs/SDK_VALIDATION.md')
$taskHashes=[ordered]@{};foreach($taskPath in ($taskSources|Sort-Object)){$taskHashes[[IO.Path]::GetRelativePath($taskRoot,$taskPath).Replace('\','/')]=(Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash.ToLowerInvariant()}
$taskCommit=(& git -C $taskRoot rev-parse HEAD).Trim()
$taskReport=[ordered]@{schema='mhwi-sdk-validation-build-v1';stage=$Stage;software_checks_passed=$true;completed_utc=[DateTime]::UtcNow.ToString('o');source_commit=$taskCommit;source_sha256=$taskHashes;checks=$taskResults;game_tested=$false;gui_tested=$false;visual_acceptance_complete=$false;formal_acceptance_complete=$false}
if($Stage -in @('Build','All')){$taskReport['dll']=[ordered]@{file='MHWEquipmentMorphSdkValidation.dll';bytes=(Get-Item -LiteralPath $taskDll).Length;sha256=(Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash.ToLowerInvariant()}}
$taskReport | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $taskReportPath -Encoding UTF8
Write-Output ('PASS: SDK validation stage '+$Stage+'; no game, Blender or UI execution')
