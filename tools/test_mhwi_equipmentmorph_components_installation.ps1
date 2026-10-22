param([string]$CorePackageDirectory,[string]$ImGuiPackageDirectory)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskVersion=(Get-Content -LiteralPath (Join-Path $taskRoot 'config/mhwi-equipmentmorph-current.json') -Raw|ConvertFrom-Json).runtime_version
if(!$CorePackageDirectory){$CorePackageDirectory=Join-Path $taskRoot ('.build/packages/MHWI-EquipmentMorph-'+$taskVersion+'-core')}
if(!$ImGuiPackageDirectory){$ImGuiPackageDirectory=Join-Path $taskRoot ('.build/packages/MHWI-EquipmentMorph-'+$taskVersion+'-imgui')}
$taskFixture=Join-Path $taskRoot ('.build/components-installation-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFixture|Out-Null
$taskChecks=0
function Check([bool]$Value,[string]$Label){if(!$Value){throw ('FAIL: '+$Label)};$script:taskChecks++}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Game([string]$Name){$taskPath=Join-Path $taskFixture ('game-'+$Name);New-Item -ItemType Directory -Path $taskPath|Out-Null;[IO.File]::WriteAllText((Join-Path $taskPath 'MonsterHunterWorld.exe'),'synthetic marker; never executed');return $taskPath}
function Run([string]$Package,[string]$Mode,[string]$Game){& (Join-Path $Package 'Manage-Installation.ps1') -Mode $Mode -GameDirectory $Game|Out-Null}
function Reject([scriptblock]$Action,[string]$Label){$taskRejected=$false;try{& $Action}catch{$taskRejected=$true};Check $taskRejected $Label}
function Manifest([string]$Package){Get-Content -LiteralPath (Join-Path $Package 'manifest.json') -Raw|ConvertFrom-Json}
function SaveManifest([string]$Package,$Value){[IO.File]::WriteAllText((Join-Path $Package 'manifest.json'),($Value|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))}
function PackageCopy([string]$Name){$taskPath=Join-Path $taskFixture $Name;Copy-Item -LiteralPath $ImGuiPackageDirectory -Destination $taskPath -Recurse;return $taskPath}
$taskCore=Manifest $CorePackageDirectory;$taskGui=Manifest $ImGuiPackageDirectory
$taskHost='nativePC/plugins/MHWEquipmentMorphConfiguredPrototype.dll';$taskPanel='nativePC/plugins/MHWEquipmentMorphImGui.dll'
$taskConfig='nativePC/plugins/MHWEquipmentMorph/runtime.ini';$taskState='nativePC/plugins/MHWEquipmentMorph/.installation/state.json'
Check (@($taskCore.files).Count -eq 1 -and $taskCore.files[0].path -eq $taskHost -and $taskCore.control_panel -eq 'none') 'core package contains only independent runtime'
Check (@($taskGui.files).Count -eq 2 -and $taskGui.files[0].path -eq $taskHost -and $taskGui.files[1].path -eq $taskPanel -and $taskGui.hotkey -eq 'Del') 'ImGui package contains runtime and optional in-game DLL'
Check (@(Get-ChildItem -LiteralPath (Join-Path $ImGuiPackageDirectory 'payload') -File -Recurse).Count -eq 2) 'ImGui payload has no retired bridge or EXE'
Check ((Test-Path -LiteralPath (Join-Path $ImGuiPackageDirectory 'licenses/LICENSE.ImGui.txt')) -and !$taskGui.formal_acceptance_complete) 'GUI license included without inherited game acceptance'
$taskGame=Game 'core-to-imgui'
$taskData=@{}
foreach($taskRelative in @('nativePC/pl/keep.txt','nativePC/plugins/OtherPlugin.dll','nativePC/plugins/MHWEquipmentMorph/presets/keep.json','nativePC/plugins/MHWEquipmentMorph/auto-presets.json','nativePC/plugins/MHWEquipmentMorph/panel-settings.json')){
 $taskPath=Join-Path $taskGame $taskRelative;New-Item -ItemType Directory -Path (Split-Path -Parent $taskPath) -Force|Out-Null;[IO.File]::WriteAllText($taskPath,'user-owned fixture; installer does not parse it');$taskData[$taskPath]=Hash $taskPath
}
Run $CorePackageDirectory Install $taskGame
Check ((Hash (Join-Path $taskGame $taskHost)) -eq $taskCore.files[0].sha256 -and !(Test-Path -LiteralPath (Join-Path $taskGame $taskPanel))) 'core install requires no panel'
Check ((Hash (Join-Path $taskGame $taskConfig)) -eq $taskCore.empty_config_sha256) 'public runtime config created from package root'
[IO.File]::AppendAllText((Join-Path $taskGame $taskConfig),"`n; modified user config`n");$taskConfigHash=Hash (Join-Path $taskGame $taskConfig)
Run $ImGuiPackageDirectory Upgrade $taskGame
Check ((Hash (Join-Path $taskGame $taskPanel)) -eq $taskGui.files[1].sha256) 'upgrade adds exact optional ImGui DLL'
$taskReceipt=Get-Content -LiteralPath (Join-Path $taskGame $taskState) -Raw|ConvertFrom-Json
Check (@($taskReceipt.files).Count -eq 2 -and @($taskReceipt.originals).Count -eq 2) 'upgrade records both managed components'
Reject {Run $CorePackageDirectory Upgrade $taskGame} 'downgrade cannot abandon managed GUI'
Run $ImGuiPackageDirectory Rollback $taskGame
Check (!(Test-Path -LiteralPath (Join-Path $taskGame $taskPanel)) -and @((Get-Content -LiteralPath (Join-Path $taskGame $taskState) -Raw|ConvertFrom-Json).files).Count -eq 1) 'rollback restores core-only installation'
Run $ImGuiPackageDirectory Upgrade $taskGame;Run $ImGuiPackageDirectory Uninstall $taskGame
Check (!(Test-Path -LiteralPath (Join-Path $taskGame $taskHost)) -and !(Test-Path -LiteralPath (Join-Path $taskGame $taskPanel)) -and !(Test-Path -LiteralPath (Join-Path $taskGame $taskState))) 'uninstall removes both new managed components'
Check ((Hash (Join-Path $taskGame $taskConfig)) -eq $taskConfigHash) 'installer leaves modified public configuration untouched'
foreach($taskPath in $taskData.Keys){Check ((Hash $taskPath) -eq $taskData[$taskPath]) 'installer leaves unowned data untouched without parsing or migrating it'}
$taskOnly=Game 'core-only-uninstall';Run $CorePackageDirectory Install $taskOnly;Run $CorePackageDirectory Uninstall $taskOnly
Check (!(Test-Path -LiteralPath (Join-Path $taskOnly $taskHost)) -and !(Test-Path -LiteralPath (Join-Path $taskOnly $taskConfig)) -and !(Test-Path -LiteralPath (Join-Path $taskOnly $taskState))) 'core-only uninstall removes its new binary, unchanged template and receipt'
$taskFresh=Game 'fresh-gui';Run $ImGuiPackageDirectory Install $taskFresh
Check ((Hash (Join-Path $taskFresh $taskHost)) -eq $taskGui.files[0].sha256 -and (Hash (Join-Path $taskFresh $taskPanel)) -eq $taskGui.files[1].sha256) 'fresh GUI install writes exactly both packaged binaries'
Run $ImGuiPackageDirectory Rollback $taskFresh
Check (!(Test-Path -LiteralPath (Join-Path $taskFresh $taskHost)) -and !(Test-Path -LiteralPath (Join-Path $taskFresh $taskPanel)) -and !(Test-Path -LiteralPath (Join-Path $taskFresh $taskConfig))) 'first-install rollback removes new binaries and unchanged template'
$taskConflict=Game 'unmanaged-gui';Run $CorePackageDirectory Install $taskConflict
[IO.File]::WriteAllText((Join-Path $taskConflict $taskPanel),'unknown unmanaged GUI');$taskConflictHash=Hash (Join-Path $taskConflict $taskPanel)
Reject {Run $ImGuiPackageDirectory Upgrade $taskConflict} 'adding GUI refuses an unknown unmanaged file'
Check ((Hash (Join-Path $taskConflict $taskPanel)) -eq $taskConflictHash -and @((Get-Content -LiteralPath (Join-Path $taskConflict $taskState) -Raw|ConvertFrom-Json).files).Count -eq 1) 'rejected GUI addition preserves unknown file and core receipt'
$taskAdopt=Game 'identical-gui';Run $CorePackageDirectory Install $taskAdopt
Copy-Item -LiteralPath (Join-Path $ImGuiPackageDirectory ('payload/'+$taskPanel)) -Destination (Join-Path $taskAdopt $taskPanel)
Run $ImGuiPackageDirectory Upgrade $taskAdopt;Run $ImGuiPackageDirectory Uninstall $taskAdopt
Check ((Hash (Join-Path $taskAdopt $taskPanel)) -eq $taskGui.files[1].sha256 -and !(Test-Path -LiteralPath (Join-Path $taskAdopt $taskHost))) 'uninstall restores identical GUI that existed before addition'
$taskTampered=Game 'tampered-gui';Run $ImGuiPackageDirectory Install $taskTampered
[IO.File]::AppendAllText((Join-Path $taskTampered $taskPanel),'changed');$taskTamperedHash=Hash (Join-Path $taskTampered $taskPanel)
Reject {Run $ImGuiPackageDirectory Uninstall $taskTampered} 'externally changed managed GUI is not overwritten'
Check ((Hash (Join-Path $taskTampered $taskPanel)) -eq $taskTamperedHash -and (Test-Path -LiteralPath (Join-Path $taskTampered $taskHost))) 'refused uninstall preserves both current files'
$taskMissing=PackageCopy 'missing-core';$taskChanged=Manifest $taskMissing;$taskChanged.files=@($taskChanged.files|Select-Object -Skip 1);SaveManifest $taskMissing $taskChanged
Reject {Run $taskMissing Install (Game 'missing-core')} 'GUI alone cannot replace mandatory core'
$taskMixed=PackageCopy 'mixed-panels';$taskChanged=Manifest $taskMixed;$taskChanged.files+=@{path='MHWI-EquipmentMorph/MHWI-EquipmentMorph-ControlPanel.exe';sha256=('0'*64)};SaveManifest $taskMixed $taskChanged
Reject {Run $taskMixed Install (Game 'mixed-panels')} 'new GUI cannot be mixed with retired panel payload'
$taskCorrupt=PackageCopy 'corrupt-payload';[IO.File]::AppendAllText((Join-Path $taskCorrupt ('payload/'+$taskPanel)),'corrupt');$taskBad=Game 'corrupt-payload'
Reject {Run $taskCorrupt Install $taskBad} 'corrupt GUI fails package validation'
Check (!(Test-Path -LiteralPath (Join-Path $taskBad $taskHost)) -and !(Test-Path -LiteralPath (Join-Path $taskBad $taskState))) 'corrupt GUI is rejected before writing core'
$taskReport=[ordered]@{schema='mhwi-components-installation-checks-v1';passed=$taskChecks;fixture_root=$taskFixture;game_directory_changed=$false;game_or_gpu_calls=$false}
[IO.File]::WriteAllText((Join-Path $taskFixture 'report.json'),($taskReport|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
$taskReport|ConvertTo-Json
