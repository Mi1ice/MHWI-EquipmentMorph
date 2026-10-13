[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'windows-toolchain.ps1')
$taskToolchain=Get-EquipmentMorphToolchain
$taskCompilerRoot=$taskToolchain.CompilerRoot
$taskSdkRoot=$taskToolchain.SdkRoot
$taskSdkVersion=$taskToolchain.SdkVersion

$taskOutput=Join-Path $taskRoot 'controls/MHWI-EquipmentMorph-ControlPanel/build'
if($OutputDirectory){$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)}
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $taskOutput 'fixture') -Force | Out-Null
$taskIncludes=@((Join-Path $taskCompilerRoot 'include'))
foreach($taskPart in @('ucrt','shared','um')){$taskIncludes+=Join-Path $taskSdkRoot ('Include/'+$taskSdkVersion+'/'+$taskPart)}
$taskLibraries=@((Join-Path $taskCompilerRoot 'lib/x64'))
foreach($taskPart in @('ucrt','um')){$taskLibraries+=Join-Path $taskSdkRoot ('Lib/'+$taskSdkVersion+'/'+$taskPart+'/x64')}
$taskCompiler=Join-Path $taskCompilerRoot 'bin/Hostx64/x64/cl.exe';$taskLinker=Join-Path $taskCompilerRoot 'bin/Hostx64/x64/link.exe'
$taskCommon=@('/nologo','/utf-8','/std:c++20','/EHsc','/MT','/O2','/W4','/WX','/fp:strict','/D_WIN32_WINNT=0x0601')+@($taskIncludes|ForEach-Object{'/I'+$_})
$taskJobs=@(
    @{Source='runtime/EquipmentMorphControlBridge/bridge.cpp';Name='MHWEquipmentMorphControlBridge';Dll=$true;Libs=@('kernel32.lib')},
    @{Source='controls/MHWI-EquipmentMorph-ControlPanel/panel.cpp';Name='MHWI-EquipmentMorph-ControlPanel';Gui=$true;Libs=@('kernel32.lib','user32.lib','gdi32.lib','comctl32.lib')},
    @{Source='runtime/EquipmentMorphControlBridge/tests.cpp';Name='ControlSessionTests';Libs=@('kernel32.lib')},
    @{Source='runtime/EquipmentMorphPerKeyCore/api.cpp';Name='fixture/MHWEquipmentMorphConfiguredPrototype';Dll=$true;Libs=@('kernel32.lib')},
    @{Source='runtime/EquipmentMorphControlBridge/pipe_tests.cpp';Name='ControlPipeTests';Libs=@('kernel32.lib')},
    @{Source='controls/MHWI-EquipmentMorph-ControlPanel/ui_tests.cpp';Name='ControlUiTests';Libs=@('kernel32.lib','user32.lib','gdi32.lib','comctl32.lib')}
)
foreach($taskJob in $taskJobs){
    $taskObject=Join-Path $taskOutput ($taskJob.Name+'.obj')
    & $taskCompiler @taskCommon '/c' (Join-Path $taskRoot $taskJob.Source) ('/Fo'+$taskObject)
    if($LASTEXITCODE -ne 0){throw ('Compilation failed: '+$taskJob.Name)}
    $taskExtension=if($taskJob.Dll){'.dll'}else{'.exe'}
    $taskArgs=@('/NOLOGO','/MACHINE:X64',('/OUT:"'+(Join-Path $taskOutput ($taskJob.Name+$taskExtension))+'"'),('"'+$taskObject+'"'))
    if($taskJob.Dll){$taskArgs+='/DLL'}
    if($taskJob.Gui){$taskArgs+='/SUBSYSTEM:WINDOWS'}
    $taskArgs+=@($taskLibraries|ForEach-Object{'/LIBPATH:"'+$_+'"'});$taskArgs+=$taskJob.Libs
    $taskResponse=Join-Path $taskOutput ($taskJob.Name+'-link.rsp');[IO.File]::WriteAllLines($taskResponse,$taskArgs,[Text.Encoding]::Unicode)
    & $taskLinker ('@'+$taskResponse)
    if($LASTEXITCODE -ne 0){throw ('Link failed: '+$taskJob.Name)}
}
$taskChecks=[ordered]@{}
foreach($taskTest in @(
    @{Name='ControlSessionTests';Field='control_session';Pattern='PASS: (\d+) control-session checks'},
    @{Name='ControlPipeTests';Field='control_pipe';Pattern='PASS: (\d+) real-pipe checks'},
    @{Name='ControlUiTests';Field='control_ui';Pattern='PASS: (\d+) native UI checks'}
)){
    $taskLines=& (Join-Path $taskOutput ($taskTest.Name+'.exe'))
    if($LASTEXITCODE -ne 0){$taskLines|Write-Host;throw ('Check failed: '+$taskTest.Name)}
    $taskLines|Write-Host
    if(($taskLines -join "`n") -notmatch $taskTest.Pattern){throw ('Check summary missing: '+$taskTest.Name)}
    $taskChecks[$taskTest.Field]=[int]$Matches[1]
}
[IO.File]::WriteAllText((Join-Path $taskOutput 'checks.json'),($taskChecks|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
