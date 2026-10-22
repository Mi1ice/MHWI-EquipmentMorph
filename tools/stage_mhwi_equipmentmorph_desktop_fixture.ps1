param([switch]$Launch)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if(@(Get-Process -Name MonsterHunterWorld -ErrorAction SilentlyContinue).Count){throw 'Exit the real game and other fixtures before staging a game-named software fixture'}
$taskOutput=Join-Path $taskRoot ('.build/desktop-integration-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $taskOutput 'fixture') -Force|Out-Null
New-Item -ItemType Directory -Path (Join-Path $taskOutput 'MHWI-EquipmentMorph') -Force|Out-Null
$taskNative=Join-Path $taskRoot '.build/current-validation/control-panel'
Copy-Item -LiteralPath (Join-Path $taskNative 'DesktopIntegrationFixture.exe') -Destination (Join-Path $taskOutput 'MonsterHunterWorld.exe')
Copy-Item -LiteralPath (Join-Path $taskNative 'fixture/MHWEquipmentMorphConfiguredPrototype.dll') -Destination (Join-Path $taskOutput 'fixture')
Copy-Item -LiteralPath (Join-Path $taskNative 'MHWEquipmentMorphControlBridge.dll') -Destination $taskOutput
Copy-Item -LiteralPath (Join-Path $taskRoot '.build/current-validation/desktop/MHWI-EquipmentMorph-ControlCenter.exe') -Destination (Join-Path $taskOutput 'MHWI-EquipmentMorph')
$taskReport=[ordered]@{schema='mhwi-control-center-fixture-v1';software_only=$true;directory=$taskOutput;game_directory_changed=$false}
if($Launch){$taskProcess=Start-Process -FilePath (Join-Path $taskOutput 'MonsterHunterWorld.exe') -WindowStyle Hidden -PassThru;$taskReport.pid=$taskProcess.Id}
$taskReport|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $taskRoot '.build/current-validation/desktop-fixture.json') -Encoding UTF8
$taskReport|ConvertTo-Json
if($Launch){$taskProcess.WaitForExit();if($taskProcess.ExitCode -ne 0){throw ('Software fixture failed: '+$taskProcess.ExitCode)}}
