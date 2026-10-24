param(
    [ValidateSet('Data','Sdk','Paired','Host','All')][string]$Stage='All',
    [Parameter(Mandatory=$true)][string]$Python,
    [string]$Blender
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskOutput=Join-Path $taskRoot '.build/capacity-validation'
New-Item -ItemType Directory -Force -Path $taskOutput | Out-Null
. (Join-Path $PSScriptRoot 'windows-toolchain.ps1')
function Build-CapacityNative([string]$Name,[string[]]$Sources) {
    $taskChain=Get-EquipmentMorphToolchain
    $taskCompiler=Join-Path $taskChain.CompilerRoot 'bin/Hostx64/x64/cl.exe'
    $taskIncludes=@((Join-Path $taskChain.CompilerRoot 'include'),(Join-Path $taskRoot 'sdk/MHWI-EquipmentMorph-0.4.0/include'))
    $taskLibraries=@((Join-Path $taskChain.CompilerRoot 'lib/x64'))
    foreach($taskPart in @('ucrt','shared','um')) { $taskIncludes+=Join-Path $taskChain.SdkRoot ('Include/'+$taskChain.SdkVersion+'/'+$taskPart) }
    foreach($taskPart in @('ucrt','um')) { $taskLibraries+=Join-Path $taskChain.SdkRoot ('Lib/'+$taskChain.SdkVersion+'/'+$taskPart+'/x64') }
    $taskExe=Join-Path $taskOutput ($Name+'.exe')
    $taskArgs=@('/nologo','/utf-8','/MT','/O2','/std:c++latest','/EHsc','/W4','/WX','/fp:strict')
    if($Name -ne 'MorphSdkCapacityTests') { $taskArgs+='/DNOMINMAX' }
    $taskArgs+=$taskIncludes | ForEach-Object { '/I'+$_ }
    $taskObjects=@()
    foreach($taskSource in $Sources) {
        $taskObject=Join-Path $taskOutput ($Name+'-'+[IO.Path]::GetFileNameWithoutExtension($taskSource)+'.obj')
        $taskCompileArgs=$taskArgs+@('/c',(Join-Path $taskRoot $taskSource),('/Fo'+$taskObject))
        & $taskCompiler @taskCompileArgs | ForEach-Object { Write-Host $_ }
        if($LASTEXITCODE -ne 0){throw "Compilation failed: $taskSource"}
        $taskObjects+=$taskObject
    }
    $taskLinkArgs=@('/NOLOGO','/MACHINE:X64','/Brepro',('/OUT:"'+$taskExe+'"'))
    $taskLinkArgs+=$taskObjects | ForEach-Object { '"'+$_+'"' }
    $taskLinkArgs+=$taskLibraries | ForEach-Object { '/LIBPATH:"'+$_+'"' }
    $taskLinkArgs+=@('kernel32.lib','bcrypt.lib')
    $taskResponse=Join-Path $taskOutput ($Name+'-link.rsp')
    [IO.File]::WriteAllLines($taskResponse,$taskLinkArgs,[Text.Encoding]::Unicode)
    & (Join-Path $taskChain.CompilerRoot 'bin/Hostx64/x64/link.exe') ('@'+$taskResponse) | ForEach-Object { Write-Host $_ }
    if($LASTEXITCODE -ne 0){throw "Link failed: $Name"}
    return $taskExe
}
$taskStages=if($Stage -eq 'All'){@('Data','Sdk','Paired','Host')}else{@($Stage)}
function Run-CapacityBlender([string]$Script,[string]$Name) {
    if(-not $Blender -or -not (Test-Path -LiteralPath $Blender)){throw 'Blender path is required for Paired validation'}
    $taskStdout=Join-Path $taskOutput ($Name+'.stdout.log')
    $taskStderr=Join-Path $taskOutput ($Name+'.stderr.log')
    $taskProcess=Start-Process -FilePath $Blender -ArgumentList @('--background','--factory-startup','--python-exit-code','1','--python',('"'+(Join-Path $taskRoot $Script)+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput $taskStdout -RedirectStandardError $taskStderr
    $taskProcess.WaitForExit()
    Get-Content -LiteralPath $taskStdout -Tail 10 | ForEach-Object { Write-Host $_ }
    if($taskProcess.ExitCode -ne 0){Get-Content -LiteralPath $taskStderr -Tail 10 | ForEach-Object { Write-Host $_ };throw "Background Blender failed: $Name exit=$($taskProcess.ExitCode)"}
}
$taskResults=@()
foreach($taskStage in $taskStages) {
    $taskPassed=$false
    $taskDetail=''
    try {
        switch($taskStage) {
            'Data' {
                $taskFixtures=Join-Path $taskOutput 'data'
                & $Python (Join-Path $PSScriptRoot 'test_morph_capacity_authoring.py') $taskFixtures
                $taskAuthoringExit=$LASTEXITCODE
                $taskExe=Build-CapacityNative 'MorphCapacityTests' @('tools/test_morph_capacity.cpp','runtime/EquipmentMorphPerKeyCore/api.cpp')
                & $taskExe $taskFixtures
                $taskNativeExit=$LASTEXITCODE
                $taskPassed=$taskAuthoringExit -eq 0 -and $taskNativeExit -eq 0
                $taskDetail="authoring=$taskAuthoringExit native=$taskNativeExit"
            }
            'Sdk' {
                $taskExe=Build-CapacityNative 'MorphSdkCapacityTests' @('sdk/MHWI-EquipmentMorph-0.4.0/tests.cpp')
                & $taskExe
                $taskPassed=$LASTEXITCODE -eq 0
                $taskDetail="sdk=$LASTEXITCODE"
            }
            'Paired' {
                $null=Build-CapacityNative 'CompareMorphPositions' @('tools/compare_morph_positions.cpp')
                Run-CapacityBlender 'tools/test_capacity_direct_blender.py' 'paired-capacity'
                Run-CapacityBlender 'tools/test_mhwi_equipmentmorph_direct_blender.py' 'paired-existing'
                Run-CapacityBlender 'tools/test_key_selection_preview.py' 'paired-key-preview'
                Run-CapacityBlender 'tools/test_geometric_normal_fallback_blender.py' 'paired-geometric-fallback'
                $taskPassed=$true
                $taskDetail='synthetic capacity and existing direct export checks completed; numerical threshold not set'
            }
            default { throw "Stage $taskStage is not implemented" }
        }
    } catch { $taskDetail=$_.Exception.Message }
    $taskResults+=@{stage=$taskStage;passed=$taskPassed;detail=$taskDetail}
}
$taskHashes=@{}
foreach($taskFile in @('tools/test_morph_capacity.cpp','tools/test_morph_capacity_authoring.py','tools/check_morph_capacity.ps1','runtime/EquipmentMorphPerKeyCore/capacity_limits.hpp','runtime/EquipmentMorphPerKeyCore/core.hpp','runtime/EquipmentMorphPerKeyCore/api.cpp','runtime/EquipmentMorphPerKeyCore/asset_pack.hpp','blender_addon_v044/mhwi_equipmentmorph/capacity.py','blender_addon_v044/mhwi_equipmentmorph/direct_export.py','blender_addon_v044/mhwi_equipmentmorph/backend/equipment_morph_export_pipeline.py','blender_addon_v044/mhwi_equipmentmorph/backend/prepare_equipment_morph_catalog.py')) {
    $taskHashes[$taskFile]=(Get-FileHash -LiteralPath (Join-Path $taskRoot $taskFile) -Algorithm SHA256).Hash.ToLowerInvariant()
}
$taskArtifacts=@{}
foreach($taskFile in @('tools/test_capacity_direct_blender.py','tools/test_geometric_normal_fallback_blender.py','tools/capture_morph_position_reference.py','tools/compare_morph_positions.cpp','tools/morph_reference_json.hpp','tools/test_morph_profile_capacity.py','blender_addon_v044/mhwi_equipmentmorph/backend/emit_equipment_morph_profile.py','blender_addon_v044/mhwi_equipmentmorph/key_selection.py','runtime/EquipmentMorphDiskConfiguration/loader.hpp')) {
    $taskPath=Join-Path $taskRoot $taskFile
    if(Test-Path -LiteralPath $taskPath){$taskHashes[$taskFile]=(Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash.ToLowerInvariant()}
}
foreach($taskFile in @('sdk/MHWI-EquipmentMorph-0.4.0/include/equipment_morph_key_client.hpp','sdk/MHWI-EquipmentMorph-0.4.0/include/equipment_morph_client.hpp','runtime/EquipmentMorphPartClient/Client.hpp','sdk/MHWI-EquipmentMorph-0.4.0/capacity_tests.hpp','sdk/MHWI-EquipmentMorph-0.4.0/tests.cpp','sdk/MHWI-EquipmentMorph-0.4.0/tests/fixtures/v040/equipment_morph_key_client.hpp','tools/build_mhwi_equipmentmorph_sdk_v04.ps1')) {
    $taskHashes[$taskFile]=(Get-FileHash -LiteralPath (Join-Path $taskRoot $taskFile) -Algorithm SHA256).Hash.ToLowerInvariant()
}
Get-ChildItem -LiteralPath $taskOutput -Filter '*.exe' | ForEach-Object { $taskArtifacts[$_.Name]=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
$taskReport=@{schema='mhwi-capacity-validation-v1';stage=$Stage;stages=$taskResults;source_sha256=$taskHashes;artifact_sha256=$taskArtifacts;synthetic_only=$true;game_tested=$false;deployable=$false}
$taskReport | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskOutput ($Stage.ToLowerInvariant()+'-report.json')) -Encoding utf8
$taskResults | ForEach-Object { Write-Host ($_.stage+': '+$_.passed+' '+$_.detail) }
if($taskResults.Where({-not $_.passed}).Count){throw 'Capacity validation failed; see stage report'}
