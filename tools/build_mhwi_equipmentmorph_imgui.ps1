param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskUpstream=Get-Content -LiteralPath (Join-Path $taskRoot 'research/imgui-v1.93.0/UPSTREAM.json') -Raw|ConvertFrom-Json
foreach($taskEntry in $taskUpstream.files){
    $taskDependency=Join-Path $taskRoot ('research/imgui-v1.93.0/'+$taskEntry.path)
    if((Get-FileHash -LiteralPath $taskDependency -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskEntry.sha256){throw ('Pinned ImGui source differs: '+$taskEntry.path)}
}
. (Join-Path $PSScriptRoot 'windows-toolchain.ps1')
$taskTools=Get-EquipmentMorphToolchain
$taskOutput=if($OutputDirectory){[IO.Path]::GetFullPath($OutputDirectory)}else{Join-Path $taskRoot '.build/current-validation/imgui'}
New-Item -ItemType Directory -Path $taskOutput -Force|Out-Null
$taskIncludes=@((Join-Path $taskTools.CompilerRoot 'include'),(Join-Path $taskRoot 'research/safetyhook-local-v0.7.0'),(Join-Path $taskRoot 'research/imgui-v1.93.0'))
foreach($taskPart in @('ucrt','shared','um','winrt')){$taskIncludes+=Join-Path $taskTools.SdkRoot ('Include/'+$taskTools.SdkVersion+'/'+$taskPart)}
$taskLibraries=@((Join-Path $taskTools.CompilerRoot 'lib/x64'))
foreach($taskPart in @('ucrt','um')){$taskLibraries+=Join-Path $taskTools.SdkRoot ('Lib/'+$taskTools.SdkVersion+'/'+$taskPart+'/x64')}
$taskCompiler=Join-Path $taskTools.CompilerRoot 'bin/Hostx64/x64/cl.exe'
$taskLinker=Join-Path $taskTools.CompilerRoot 'bin/Hostx64/x64/link.exe'
$taskCommon=@('/nologo','/utf-8','/std:c++latest','/EHsc','/MT','/O2','/DUNICODE','/D_UNICODE','/DZYDIS_STATIC_BUILD','/DZYCORE_STATIC_BUILD','/D_WIN32_WINNT=0x0A00')+@($taskIncludes|ForEach-Object{'/I'+$_})
$taskSources=@('research/safetyhook-local-v0.7.0/Zydis.c','research/safetyhook-local-v0.7.0/safetyhook.cpp','research/imgui-v1.93.0/imgui.cpp','research/imgui-v1.93.0/imgui_draw.cpp','research/imgui-v1.93.0/imgui_tables.cpp','research/imgui-v1.93.0/imgui_widgets.cpp','research/imgui-v1.93.0/backends/imgui_impl_dx12.cpp','research/imgui-v1.93.0/backends/imgui_impl_win32.cpp','controls/MHWI-EquipmentMorph-ImGui/overlay.cpp')
$taskObjects=@()
foreach($taskSource in $taskSources){
    $taskObject=Join-Path $taskOutput ([IO.Path]::GetFileNameWithoutExtension($taskSource)+'.obj');$taskObjects+=$taskObject
    $taskFlags=@(if($taskSource.EndsWith('Zydis.c')){'/TC';'/W3'}elseif($taskSource.StartsWith('controls/')){'/W4';'/WX';'/fp:strict'}else{'/W3'})
    & $taskCompiler @taskCommon @taskFlags '/c' (Join-Path $taskRoot $taskSource) ('/Fo'+$taskObject)
    if($LASTEXITCODE -ne 0){throw ('ImGui compilation failed: '+$taskSource)}
}
$taskArguments=@('/NOLOGO','/DLL','/MACHINE:X64','/Brepro',('/OUT:"'+(Join-Path $taskOutput 'MHWEquipmentMorphImGui.dll')+'"'))+@($taskObjects|ForEach-Object{'"'+$_+'"'})+@($taskLibraries|ForEach-Object{'/LIBPATH:"'+$_+'"'})+@('kernel32.lib','user32.lib','gdi32.lib','imm32.lib','d3d12.lib','dxgi.lib','dxguid.lib','dinput8.lib','d3dcompiler.lib','ole32.lib','dwmapi.lib')
$taskResponse=Join-Path $taskOutput 'overlay-link.rsp';[IO.File]::WriteAllLines($taskResponse,$taskArguments,[Text.Encoding]::Unicode)
& $taskLinker ('@'+$taskResponse);if($LASTEXITCODE -ne 0){throw 'ImGui DLL link failed'}
& $taskCompiler @taskCommon '/W4' '/WX' '/fp:strict' '/c' (Join-Path $taskRoot 'controls/MHWI-EquipmentMorph-ImGui/fixture.cpp') ('/Fo'+(Join-Path $taskOutput 'fixture.obj'))
if($LASTEXITCODE -ne 0){throw 'ImGui fixture compilation failed'}
$taskArguments=@('/NOLOGO','/SUBSYSTEM:WINDOWS','/MACHINE:X64','/Brepro',('/OUT:"'+(Join-Path $taskOutput 'EquipmentMorphImGuiFixture.exe')+'"'),('"'+(Join-Path $taskOutput 'fixture.obj')+'"'))+@($taskLibraries|ForEach-Object{'/LIBPATH:"'+$_+'"'})+@('kernel32.lib','user32.lib','d3d12.lib','dxgi.lib','dxguid.lib','dinput8.lib')
$taskResponse=Join-Path $taskOutput 'fixture-link.rsp';[IO.File]::WriteAllLines($taskResponse,$taskArguments,[Text.Encoding]::Unicode)
& $taskLinker ('@'+$taskResponse);if($LASTEXITCODE -ne 0){throw 'ImGui fixture link failed'}
& $taskCompiler @taskCommon '/W4' '/WX' '/fp:strict' '/c' (Join-Path $taskRoot 'controls/MHWI-EquipmentMorph-ImGui/control_tests.cpp') ('/Fo'+(Join-Path $taskOutput 'control-tests.obj'))
if($LASTEXITCODE -ne 0){throw 'ImGui control tests compilation failed'}
$taskArguments=@('/NOLOGO','/SUBSYSTEM:CONSOLE','/MACHINE:X64','/Brepro',('/OUT:"'+(Join-Path $taskOutput 'ImGuiControlTests.exe')+'"'),('"'+(Join-Path $taskOutput 'control-tests.obj')+'"'))+@($taskLibraries|ForEach-Object{'/LIBPATH:"'+$_+'"'})+@('kernel32.lib')
$taskResponse=Join-Path $taskOutput 'control-tests-link.rsp';[IO.File]::WriteAllLines($taskResponse,$taskArguments,[Text.Encoding]::Unicode)
& $taskLinker ('@'+$taskResponse);if($LASTEXITCODE -ne 0){throw 'ImGui control tests link failed'}
$taskChecks=& (Join-Path $taskOutput ImGuiControlTests.exe)
if($LASTEXITCODE -ne 0){$taskChecks|Write-Output;throw 'ImGui control tests failed'}
$taskChecks|Write-Output
if(($taskChecks -join "`n") -notmatch 'PASS: (\d+) ImGui control checks'){throw 'Missing control test summary'}
$taskPassed=[int]$Matches[1]
Copy-Item -LiteralPath (Join-Path $taskRoot 'research/imgui-v1.93.0/LICENSE.txt') -Destination (Join-Path $taskOutput 'LICENSE.ImGui.txt') -Force
foreach($taskLicense in @('LICENSE.SafetyHook','LICENSE.Zydis')){Copy-Item -LiteralPath (Join-Path $taskRoot ('research/safetyhook-local-v0.7.0/'+$taskLicense)) -Destination $taskOutput -Force}
Copy-Item -LiteralPath (Join-Path $taskRoot 'controls/MHWI-EquipmentMorph-ImGui/README.md') -Destination $taskOutput -Force
$taskManifest=[ordered]@{schema='mhwi-equipmentmorph-imgui-candidate-v1';stage=3;architecture='windows-x64';renderer='dx12';formal_acceptance_complete=$false;morph_api_connected=$true;control_checks=$taskPassed;ui_testing='user-operated';imgui_commit=$taskUpstream.commit;files=@()}
foreach($taskFile in @('MHWEquipmentMorphImGui.dll','EquipmentMorphImGuiFixture.exe','ImGuiControlTests.exe','README.md','LICENSE.ImGui.txt','LICENSE.SafetyHook','LICENSE.Zydis')){
    $taskManifest.files+=@{path=$taskFile;sha256=(Get-FileHash -LiteralPath (Join-Path $taskOutput $taskFile) -Algorithm SHA256).Hash.ToLowerInvariant()}
}
[IO.File]::WriteAllText((Join-Path $taskOutput 'manifest.json'),($taskManifest|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
Write-Output ('PASS: optional stage-3 ImGui DLL built at '+$taskOutput+'; UI tests left to user')
