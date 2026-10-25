param([ValidateSet('Session','Control','Preset','Storage','Regression','All')][string]$Stage='All',[string]$Python='python',[string]$CoreCandidate)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskOutput=Join-Path $taskRoot '.build/panel-capacity'
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskReportPath=Join-Path $taskOutput ($Stage.ToLowerInvariant()+'-report.json')
@{schema='mhwi-panel-capacity-v1';stage=$Stage;software_checks_passed=$false;reason='in_progress_or_failed'} | ConvertTo-Json | Set-Content -LiteralPath $taskReportPath -Encoding UTF8
if($Stage -ne 'All'){@{schema='mhwi-panel-capacity-v1';stage='All';software_checks_passed=$false;reason='run_All_for_candidate'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskOutput 'all-report.json') -Encoding UTF8}
. (Join-Path $PSScriptRoot 'windows-toolchain.ps1')
$taskTools=Get-EquipmentMorphToolchain
$taskCompiler=Join-Path $taskTools.CompilerRoot 'bin/Hostx64/x64/cl.exe'
$taskLabels=& (Join-Path $PSScriptRoot 'build_imgui_equipment_labels.ps1') -Output (Join-Path $taskOutput 'EquipmentLabels.generated.hpp')
$taskIncludes=@($taskOutput,(Join-Path $taskTools.CompilerRoot 'include'))
foreach($taskPart in @('ucrt','shared','um')){$taskIncludes+=Join-Path $taskTools.SdkRoot ('Include/'+$taskTools.SdkVersion+'/'+$taskPart)}
$taskLibraries=@((Join-Path $taskTools.CompilerRoot 'lib/x64'))
foreach($taskPart in @('ucrt','um')){$taskLibraries+=Join-Path $taskTools.SdkRoot ('Lib/'+$taskTools.SdkVersion+'/'+$taskPart+'/x64')}
$taskCommon=@('/nologo','/utf-8','/std:c++20','/EHsc','/MT','/O2','/W4','/WX','/fp:strict')+@($taskIncludes | ForEach-Object {'/I'+$_})
function Build-PanelTest([string]$Source,[string]$Name){
    $taskExe=Join-Path $taskOutput ($Name+'.exe')
    & $taskCompiler @taskCommon (Join-Path $taskRoot $Source) ('/Fo'+(Join-Path $taskOutput ($Name+'.obj'))) ('/Fe'+$taskExe) /link @($taskLibraries | ForEach-Object {'/LIBPATH:'+$_}) kernel32.lib ole32.lib | Out-Host
    if($LASTEXITCODE -ne 0){throw ('Compile failed: '+$Name)}
    return $taskExe
}
$taskExe=Build-PanelTest 'controls/MHWI-EquipmentMorph-ImGui/panel_capacity_tests.cpp' 'PanelCapacityTests'
$taskResults=[ordered]@{}
$taskStages=if($Stage -eq 'All'){@('Session','Control','Preset','Storage','Regression')}else{@($Stage)}
foreach($taskStage in $taskStages){
    $taskLines=@(& $taskExe $taskStage);$taskExit=$LASTEXITCODE;$taskLines | Out-Host
    if($taskExit -ne 0){throw ('Checks failed: '+$taskStage)}
    $taskMatch=[regex]::Match(($taskLines -join "`n"),'PASS: (\d+) panel capacity checks')
    if(!$taskMatch.Success){throw 'Missing assertion count'}
    $taskResults[$taskStage]=@{passed=$true;checks=[int]$taskMatch.Groups[1].Value}
}
if($Stage -in @('Session','All')){
    $taskLegacy=Build-PanelTest 'runtime/EquipmentMorphControlBridge/tests.cpp' 'LegacySessionTests'
    & $taskLegacy
    if($LASTEXITCODE -ne 0){throw 'Legacy session checks failed'}
}
if($Stage -eq 'Control'){
    $taskControl=Build-PanelTest 'controls/MHWI-EquipmentMorph-ImGui/control_tests.cpp' 'ImGuiControlTests'
    & $taskControl
    if($LASTEXITCODE -ne 0){throw 'Existing control checks failed'}
}
if($Stage -in @('Control','Preset','Storage')){
    $taskPreset=Build-PanelTest 'controls/MHWI-EquipmentMorph-ImGui/preset_tests.cpp' 'ImGuiPresetTests'
    & $taskPreset
    if($LASTEXITCODE -ne 0){throw 'Existing preset checks failed'}
}
if($Stage -in @('Regression','All')){
    if(!$CoreCandidate -or !(Test-Path -LiteralPath $CoreCandidate -PathType Leaf)){throw 'Regression requires the verified existing core candidate'}
    & (Join-Path $PSScriptRoot 'build_mhwi_equipmentmorph_imgui.ps1') -OutputDirectory (Join-Path $taskOutput 'imgui') -CoreCandidate $CoreCandidate | Out-Host
    if($LASTEXITCODE -ne 0){throw 'ImGui integration build failed'}
    $taskBuild=Get-Content -LiteralPath (Join-Path $taskOutput 'imgui/manifest.json') -Raw | ConvertFrom-Json
    $taskResults['integration']=@{passed=$true;input_checks=$taskBuild.input_checks;control_checks=$taskBuild.control_checks;preset_checks=$taskBuild.preset_checks;fixture_built=$true;fixture_run=$false}
    $taskSdkPath=Join-Path $taskOutput 'sdk-validation-regression/all-report.json'
    $taskReuse=Test-Path -LiteralPath $taskSdkPath
    if($taskReuse){
        $taskSdk=Get-Content -LiteralPath $taskSdkPath -Raw | ConvertFrom-Json
        $taskReuse=$taskSdk.software_checks_passed -eq $true
        foreach($taskProperty in $taskSdk.source_sha256.PSObject.Properties){
            $taskSource=Join-Path $taskRoot $taskProperty.Name
            if(!(Test-Path -LiteralPath $taskSource -PathType Leaf) -or (Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskProperty.Value){$taskReuse=$false;break}
        }
    }
    if(!$taskReuse){
        & (Join-Path $PSScriptRoot 'check_sdk_validation.ps1') -Stage All -Python $Python -OutputDirectory (Join-Path $taskOutput 'sdk-validation-regression') | Out-Host
        if($LASTEXITCODE -ne 0){throw 'SDK validation regression failed'}
        $taskSdk=Get-Content -LiteralPath $taskSdkPath -Raw | ConvertFrom-Json
    }
    $taskResults['sdk_regression']=@{passed=$true;reused_unchanged_sources=$taskReuse;source_commit=$taskSdk.source_commit;checks=$taskSdk.checks;report_sha256=(Get-FileHash -LiteralPath $taskSdkPath -Algorithm SHA256).Hash.ToLowerInvariant()}
    $taskCoreHash=(Get-FileHash -LiteralPath $CoreCandidate -Algorithm SHA256).Hash.ToLowerInvariant()
}
$taskHashes=[ordered]@{}
$taskSources=@(& git -C $taskRoot -c core.quotepath=false ls-files)+@(Get-ChildItem (Join-Path $taskRoot 'controls/MHWI-EquipmentMorph-ImGui') -File | ForEach-Object {[IO.Path]::GetRelativePath($taskRoot,$_.FullName).Replace('\','/')})
foreach($taskSource in ($taskSources | Sort-Object -Unique)){if(Test-Path -LiteralPath (Join-Path $taskRoot $taskSource) -PathType Leaf){$taskHashes[$taskSource]=(Get-FileHash -LiteralPath (Join-Path $taskRoot $taskSource) -Algorithm SHA256).Hash.ToLowerInvariant()}}
$taskReport=[ordered]@{schema='mhwi-panel-capacity-v1';stage=$Stage;software_checks_passed=$true;completed_utc=[DateTime]::UtcNow.ToString('o');source_commit=(& git -C $taskRoot rev-parse HEAD).Trim();source_sha256=$taskHashes;checks=$taskResults;gui_tested=$false;game_tested=$false;formal_acceptance_complete=$false}
if($Stage -in @('Regression','All')){$taskReport['core_sha256']=$taskCoreHash;$taskReport['dll']=@{file='imgui/MHWEquipmentMorphImGui.dll';bytes=(Get-Item -LiteralPath (Join-Path $taskOutput 'imgui/MHWEquipmentMorphImGui.dll')).Length;sha256=(Get-FileHash -LiteralPath (Join-Path $taskOutput 'imgui/MHWEquipmentMorphImGui.dll') -Algorithm SHA256).Hash.ToLowerInvariant()};$taskReport['imgui_manifest_sha256']=(Get-FileHash -LiteralPath (Join-Path $taskOutput 'imgui/manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant()}
$taskReport | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $taskReportPath -Encoding UTF8
