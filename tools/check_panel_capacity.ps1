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
    $taskLines=@(& $taskExe $taskStage);$taskExit=$LASTEXITCODE;$taskLines | Write-Output
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
if($Stage -in @('Control','All')){
    $taskControl=Build-PanelTest 'controls/MHWI-EquipmentMorph-ImGui/control_tests.cpp' 'ImGuiControlTests'
    & $taskControl
    if($LASTEXITCODE -ne 0){throw 'Existing control checks failed'}
}
if($Stage -in @('Control','Preset','Storage','All')){
    $taskPreset=Build-PanelTest 'controls/MHWI-EquipmentMorph-ImGui/preset_tests.cpp' 'ImGuiPresetTests'
    & $taskPreset
    if($LASTEXITCODE -ne 0){throw 'Existing preset checks failed'}
}
$taskHashes=[ordered]@{}
Get-ChildItem (Join-Path $taskRoot 'controls/MHWI-EquipmentMorph-ImGui') -File | Where-Object {$_.Extension -in @('.hpp','.cpp')} | Sort-Object FullName | ForEach-Object {$taskHashes[[IO.Path]::GetRelativePath($taskRoot,$_.FullName).Replace('\','/')]=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
$taskHashes['tools/check_panel_capacity.ps1']=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
@{schema='mhwi-panel-capacity-v1';stage=$Stage;software_checks_passed=$true;source_commit=(& git -C $taskRoot rev-parse HEAD).Trim();source_sha256=$taskHashes;checks=$taskResults;gui_tested=$false;game_tested=$false;formal_acceptance_complete=$false} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $taskReportPath -Encoding UTF8
