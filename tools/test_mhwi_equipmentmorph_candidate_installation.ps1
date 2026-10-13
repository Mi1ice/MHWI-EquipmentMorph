$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskCurrent=Get-Content -LiteralPath (Join-Path $taskRoot 'config/mhwi-equipmentmorph-current.json') -Raw|ConvertFrom-Json
$taskSource=Join-Path $taskRoot $taskCurrent.release_directory
$taskFixtureRoot=Join-Path $taskRoot ('.build/generic-installation-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskFixtureRoot | Out-Null
$taskChecks=0
function Check([bool]$Condition,[string]$Label){if(-not $Condition){throw ('FAIL: '+$Label)};$script:taskChecks++}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function MakeGame([string]$Name){$taskPath=Join-Path $taskFixtureRoot $Name;New-Item -ItemType Directory -Path $taskPath | Out-Null;[IO.File]::WriteAllText((Join-Path $taskPath 'MonsterHunterWorld.exe'),'synthetic directory marker; never executed');return $taskPath}
function Run([string]$Package,[string]$Mode,[string]$Game){& (Join-Path $Package 'Manage-Installation.ps1') -Mode $Mode -GameDirectory $Game | Out-Null}
function Reject([scriptblock]$Action,[string]$Label){$taskRejected=$false;try{& $Action}catch{$taskRejected=$true};Check $taskRejected $Label}
$taskManifest=Get-Content -LiteralPath (Join-Path $taskSource 'manifest.json') -Raw | ConvertFrom-Json
$taskHost=$taskManifest.files[0].path;$taskBridge=$taskManifest.files[1].path;$taskPanel=$taskManifest.files[2].path
$taskConfig='nativePC/plugins/MHWEquipmentMorph/runtime.ini';$taskState='nativePC/plugins/MHWEquipmentMorph/.installation/state.json'
$taskGame=MakeGame 'fresh'
$taskUnrelated=Join-Path $taskGame 'nativePC/plugins/OtherPlugin.dll';New-Item -ItemType Directory -Path (Split-Path -Parent $taskUnrelated) | Out-Null;[IO.File]::WriteAllText($taskUnrelated,'unrelated plugin');$taskUnrelatedHash=Hash $taskUnrelated
Run $taskSource Install $taskGame
Check ((Hash (Join-Path $taskGame $taskHost)) -eq $taskManifest.files[0].sha256 -and (Hash (Join-Path $taskGame $taskBridge)) -eq $taskManifest.files[1].sha256) 'fresh install exact binaries'
Check ((Hash (Join-Path $taskGame $taskConfig)) -eq $taskManifest.empty_config_sha256) 'fresh install empty configuration'
Check ((Hash $taskUnrelated) -eq $taskUnrelatedHash) 'unrelated plugin preserved'
Reject {Run $taskSource Install $taskGame} 'duplicate install rejected'
[IO.File]::AppendAllText((Join-Path $taskGame $taskConfig),"`n; user changes preserved`n");$taskUserConfigHash=Hash (Join-Path $taskGame $taskConfig)
$taskData=Join-Path $taskGame 'nativePC/plugins/MHWEquipmentMorph/packages/user-package/keep.txt';New-Item -ItemType Directory -Path (Split-Path -Parent $taskData) | Out-Null;[IO.File]::WriteAllText($taskData,'synthetic user-owned package marker')
$taskUpgrade=Join-Path $taskFixtureRoot 'upgrade-package';Copy-Item -LiteralPath $taskSource -Destination $taskUpgrade -Recurse
$taskNext=Get-Content -LiteralPath (Join-Path $taskUpgrade 'manifest.json') -Raw | ConvertFrom-Json
$taskNext.version='fixture-next';[IO.File]::WriteAllText((Join-Path $taskUpgrade ('payload/'+$taskBridge)),'synthetic next bridge; never loaded');$taskNext.files[1].sha256=Hash (Join-Path $taskUpgrade ('payload/'+$taskBridge))
[IO.File]::WriteAllText((Join-Path $taskUpgrade 'manifest.json'),($taskNext | ConvertTo-Json -Depth 6))
Run $taskUpgrade Upgrade $taskGame
Check ((Hash (Join-Path $taskGame $taskBridge)) -eq $taskNext.files[1].sha256) 'upgrade changed only expected binary'
Check ((Hash (Join-Path $taskGame $taskConfig)) -eq $taskUserConfigHash -and (Test-Path -LiteralPath $taskData)) 'upgrade preserves configuration and packages'
Run $taskUpgrade Rollback $taskGame
Check ((Hash (Join-Path $taskGame $taskBridge)) -eq $taskManifest.files[1].sha256 -and ((Get-Content -LiteralPath (Join-Path $taskGame $taskState) -Raw|ConvertFrom-Json).version -eq $taskManifest.version)) 'rollback exact previous version and receipt'
Check ((Hash (Join-Path $taskGame $taskConfig)) -eq $taskUserConfigHash) 'rollback preserves modified configuration'
$taskOriginal=Hash (Join-Path $taskGame $taskHost);[IO.File]::AppendAllText((Join-Path $taskGame $taskHost),'modified managed binary')
Reject {Run $taskUpgrade Upgrade $taskGame} 'tampered managed binary blocks upgrade'
Check ((Hash (Join-Path $taskGame $taskBridge)) -eq $taskManifest.files[1].sha256) 'blocked upgrade performs no binary changes'
Copy-Item -LiteralPath (Join-Path $taskSource ('payload/'+$taskHost)) -Destination (Join-Path $taskGame $taskHost) -Force
Check ((Hash (Join-Path $taskGame $taskHost)) -eq $taskOriginal) 'fixture restored before uninstall'
Run $taskSource Uninstall $taskGame
Check (-not (Test-Path -LiteralPath (Join-Path $taskGame $taskHost)) -and -not (Test-Path -LiteralPath (Join-Path $taskGame $taskBridge)) -and -not (Test-Path -LiteralPath (Join-Path $taskGame $taskPanel))) 'uninstall removes only newly installed managed binaries'
Check ((Hash (Join-Path $taskGame $taskConfig)) -eq $taskUserConfigHash -and (Test-Path -LiteralPath $taskData) -and (Hash $taskUnrelated) -eq $taskUnrelatedHash) 'uninstall preserves user changes and unrelated plugin'
$taskSame=MakeGame 'adopt-identical';New-Item -ItemType Directory -Path (Split-Path -Parent (Join-Path $taskSame $taskHost)) -Force|Out-Null;Copy-Item -LiteralPath (Join-Path $taskSource ('payload/'+$taskHost)) -Destination (Join-Path $taskSame $taskHost)
Run $taskSource Install $taskSame;Run $taskSource Uninstall $taskSame
Check ((Hash (Join-Path $taskSame $taskHost)) -eq $taskManifest.files[0].sha256 -and -not (Test-Path -LiteralPath (Join-Path $taskSame $taskConfig))) 'uninstall restores preexisting identical host and removes unchanged created config'
$taskUnknown=MakeGame 'unknown-conflict';New-Item -ItemType Directory -Path (Split-Path -Parent (Join-Path $taskUnknown $taskHost)) -Force|Out-Null;[IO.File]::WriteAllText((Join-Path $taskUnknown $taskHost),'unknown host');$taskUnknownHash=Hash (Join-Path $taskUnknown $taskHost)
Reject {Run $taskSource Install $taskUnknown} 'unknown unmanaged host refused'
Check ((Hash (Join-Path $taskUnknown $taskHost)) -eq $taskUnknownHash -and -not (Test-Path -LiteralPath (Join-Path $taskUnknown $taskState))) 'unknown file preserved without receipt'
$taskBad=Join-Path $taskFixtureRoot 'bad-package';Copy-Item -LiteralPath $taskSource -Destination $taskBad -Recurse;[IO.File]::AppendAllText((Join-Path $taskBad ('payload/'+$taskHost)),'corrupt')
$taskBadGame=MakeGame 'corrupt-payload';Reject {Run $taskBad Install $taskBadGame} 'corrupt distribution rejected before mutation'
Check (-not (Test-Path -LiteralPath (Join-Path $taskBadGame $taskHost))) 'corrupt package leaves target clean'
$taskTraversal=Join-Path $taskFixtureRoot 'traversal-package';Copy-Item -LiteralPath $taskSource -Destination $taskTraversal -Recurse;$taskChanged=Get-Content -LiteralPath (Join-Path $taskTraversal 'manifest.json') -Raw|ConvertFrom-Json;$taskChanged.files[0].path='../escape.dll';[IO.File]::WriteAllText((Join-Path $taskTraversal 'manifest.json'),($taskChanged|ConvertTo-Json -Depth 6))
Reject {Run $taskTraversal Install $taskBadGame} 'path traversal outside allowlist refused'
$taskFailed=MakeGame 'late-copy-failure';[IO.File]::WriteAllText((Join-Path $taskFailed 'MHWI-EquipmentMorph'),'file obstructing panel directory')
Reject {Run $taskSource Install $taskFailed} 'late copy failure reported'
Check (-not (Test-Path -LiteralPath (Join-Path $taskFailed $taskHost)) -and -not (Test-Path -LiteralPath (Join-Path $taskFailed $taskBridge)) -and -not (Test-Path -LiteralPath (Join-Path $taskFailed $taskState))) 'late failure restores all prior targets'
$taskRollback=MakeGame 'fresh-rollback';Run $taskSource Install $taskRollback;Run $taskSource Rollback $taskRollback
Check (-not (Test-Path -LiteralPath (Join-Path $taskRollback $taskHost)) -and -not (Test-Path -LiteralPath (Join-Path $taskRollback $taskConfig))) 'first-install rollback removes only own unchanged files'
$taskConcurrent=MakeGame 'concurrent-operation'
$taskHasher=[Security.Cryptography.SHA256]::Create();try{$taskDigest=[BitConverter]::ToString($taskHasher.ComputeHash([Text.Encoding]::UTF8.GetBytes(([IO.Path]::GetFullPath($taskConcurrent)).ToLowerInvariant()))).Replace('-','')}finally{$taskHasher.Dispose()}
$taskMutex=[Threading.Mutex]::new($false,('Local\MHWI-EquipmentMorph-Install-'+$taskDigest));$null=$taskMutex.WaitOne(0)
try{
    $taskCommand="& '"+(Join-Path $taskSource 'Manage-Installation.ps1').Replace("'","''")+"' -Mode Install -GameDirectory '"+$taskConcurrent.Replace("'","''")+"'"
    $taskEncoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($taskCommand))
    $taskChild=Start-Process -FilePath (Get-Process -Id $PID).Path -ArgumentList @('-NoProfile','-NonInteractive','-EncodedCommand',$taskEncoded) -WindowStyle Hidden -Wait -PassThru -RedirectStandardError (Join-Path $taskFixtureRoot 'concurrent-error.txt')
    Check ($taskChild.ExitCode -ne 0) 'second process is rejected by installation mutex'
    Check (-not (Test-Path -LiteralPath (Join-Path $taskConcurrent $taskState))) 'concurrent rejection does not create installation state'
}finally{$taskMutex.ReleaseMutex();$taskMutex.Dispose()}
$taskJunction=MakeGame 'junction-operation';$taskOutside=Join-Path $taskFixtureRoot 'outside-game';New-Item -ItemType Directory -Path $taskOutside|Out-Null
$taskSentinel=Join-Path $taskOutside 'preserve.txt';[IO.File]::WriteAllText($taskSentinel,'outside-game sentinel');$taskSentinelHash=Hash $taskSentinel
New-Item -ItemType Directory -Path (Join-Path $taskJunction 'nativePC')|Out-Null;New-Item -ItemType Junction -Path (Join-Path $taskJunction 'nativePC/plugins') -Target $taskOutside|Out-Null
Reject {Run $taskSource Install $taskJunction} 'junction inside managed destination is refused'
Check ((Hash $taskSentinel) -eq $taskSentinelHash -and -not (Test-Path -LiteralPath (Join-Path $taskOutside 'MHWEquipmentMorphConfiguredPrototype.dll'))) 'junction cannot redirect managed writes outside named game directory'
$taskReport=[ordered]@{schema='mhwi-equipmentmorph-candidate-installation-v1';runtime_version=$taskCurrent.runtime_version;passed=$taskChecks;fixture_root=$taskFixtureRoot;game_directory_changed=$false;game_or_gpu_calls=$false}
$taskReportDirectory=Join-Path $taskRoot '.build/current-validation';New-Item -ItemType Directory -Path $taskReportDirectory -Force|Out-Null
[IO.File]::WriteAllText((Join-Path $taskReportDirectory 'installation-checks.json'),($taskReport|ConvertTo-Json -Depth 4),[Text.UTF8Encoding]::new($false))
$taskReport|ConvertTo-Json -Depth 4
