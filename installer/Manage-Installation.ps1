[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Install','Upgrade','Rollback','Uninstall')][string]$Mode,
    [Parameter(Mandatory)][string]$GameDirectory
)
$ErrorActionPreference='Stop'
$taskPackageRoot=$PSScriptRoot
$taskGameRoot=[IO.Path]::GetFullPath($GameDirectory).TrimEnd([IO.Path]::DirectorySeparatorChar)
$taskAllowed=@('nativePC/plugins/MHWEquipmentMorphConfiguredPrototype.dll','nativePC/plugins/MHWEquipmentMorphControlBridge.dll','MHWI-EquipmentMorph/MHWI-EquipmentMorph-ControlPanel.exe')
$taskConfig='nativePC/plugins/MHWEquipmentMorph/runtime.ini'
$taskStateRelative='nativePC/plugins/MHWEquipmentMorph/.installation/state.json'
$taskStateRootRelative='nativePC/plugins/MHWEquipmentMorph/.installation'
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Target([string]$Relative){
    if([IO.Path]::IsPathRooted($Relative)-or $Relative.Contains(':')-or @(($Relative -split '[/\\]') | Where-Object {$_ -eq '..' -or $_ -eq '.' -or $_ -eq ''}).Count){throw 'Invalid managed path'}
    $taskFull=[IO.Path]::GetFullPath((Join-Path $taskGameRoot $Relative))
    if(-not $taskFull.StartsWith($taskGameRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Managed path escapes game directory'}
    $taskCheck=$taskFull
    while($taskCheck.Length -ge $taskGameRoot.Length){
        if(Test-Path -LiteralPath $taskCheck){if((Get-Item -LiteralPath $taskCheck -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Managed path contains a junction or symlink'}}
        if($taskCheck -eq $taskGameRoot){break};$taskCheck=Split-Path -Parent $taskCheck
    }
    return $taskFull
}
function Exited{
    $taskExecutable=Target 'MonsterHunterWorld.exe'
    foreach($taskProcess in @(Get-CimInstance Win32_Process -Filter "Name='MonsterHunterWorld.exe'")){
        if(-not $taskProcess.ExecutablePath -or [IO.Path]::GetFullPath($taskProcess.ExecutablePath).Equals($taskExecutable,[StringComparison]::OrdinalIgnoreCase)){throw 'Normally exit this game before managing plugins'}
    }
}
function WriteJson([string]$Path,$Value){
    $taskTemporary=$Path+'.tmp';[IO.File]::WriteAllText($taskTemporary,($Value|ConvertTo-Json -Depth 40),[Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $taskTemporary -Destination $Path -Force
}
function CopyManaged([string]$Source,[string]$Destination){
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}
function ValidateState($State){
    if($State.schema -ne 'mhwi-equipmentmorph-install-state-v1' -or -not ([string]$State.game_directory).Equals($taskGameRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Installation state belongs to another directory/version'}
    if(@($State.files).Count -ne 3 -or @($State.originals).Count -ne 3){throw 'Invalid managed file count'}
    foreach($taskList in @(@($State.files),@($State.originals))){
        $taskSeen=@{};foreach($taskEntry in $taskList){
            if($taskEntry.path -notin $taskAllowed -or $taskSeen.ContainsKey($taskEntry.path)){throw 'Invalid state file path'};$taskSeen[$taskEntry.path]=$true
            $null=Target $taskEntry.path
            if($taskEntry.backup){ValidateBackup $taskEntry}
        }
    }
}
function ValidateBackup($Entry){
    if(-not $Entry.backup.StartsWith($taskStateRootRelative+'/',[StringComparison]::Ordinal) -or $Entry.sha256 -notmatch '^[0-9a-f]{64}$'){throw 'Invalid backup reference'}
    if((Hash (Target $Entry.backup)) -ne $Entry.sha256){throw 'Backup differs from receipt'}
}
function VerifyCurrent($State){
    foreach($taskEntry in $State.files){if(-not (Test-Path -LiteralPath (Target $taskEntry.path)) -or (Hash (Target $taskEntry.path)) -ne $taskEntry.sha256){throw 'Managed binary was modified; leave it unchanged and resolve the difference first'}}
}
function RestoreEntries($Entries){
    foreach($taskEntry in $Entries){
        $taskDestination=Target $taskEntry.path
        if($taskEntry.existed){CopyManaged (Target $taskEntry.backup) $taskDestination}
        elseif(Test-Path -LiteralPath $taskDestination){Remove-Item -LiteralPath $taskDestination}
    }
}
if(-not (Test-Path -LiteralPath $taskGameRoot -PathType Container)){throw 'Game directory missing'}
if(-not (Test-Path -LiteralPath (Target 'MonsterHunterWorld.exe') -PathType Leaf)){throw 'Select the directory containing MonsterHunterWorld.exe'}
Exited
$taskHasher=[Security.Cryptography.SHA256]::Create()
try{$taskLockDigest=[BitConverter]::ToString($taskHasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($taskGameRoot.ToLowerInvariant()))).Replace('-','')}finally{$taskHasher.Dispose()}
$taskLockName='Local\MHWI-EquipmentMorph-Install-'+$taskLockDigest
$taskLock=[Threading.Mutex]::new($false,$taskLockName);$taskLocked=$false
try {
try{$taskLocked=$taskLock.WaitOne(0)}catch [Threading.AbandonedMutexException]{$taskLocked=$true}
if(-not $taskLocked){throw 'Another installation operation is running for this game directory'}
$taskStatePath=Target $taskStateRelative;$taskState=$null
if(Test-Path -LiteralPath $taskStatePath){$taskState=Get-Content -LiteralPath $taskStatePath -Raw | ConvertFrom-Json;ValidateState $taskState;VerifyCurrent $taskState}
$taskTransaction=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfff')+'-'+[Guid]::NewGuid().ToString('N')
$taskTransactionRelative=$taskStateRootRelative+'/'+$taskTransaction
$taskTransactionRoot=Target $taskTransactionRelative
if($Mode -eq 'Rollback' -or $Mode -eq 'Uninstall'){
    if(-not $taskState){throw 'No managed installation to restore'}
    if($Mode -eq 'Rollback'){
        $taskUndoReference=[string]$taskState.undo
        if(-not $taskUndoReference.StartsWith($taskStateRootRelative+'/',[StringComparison]::Ordinal)){throw 'Invalid undo reference'}
        $taskUndo=Get-Content -LiteralPath (Target $taskUndoReference) -Raw | ConvertFrom-Json
        if($taskUndo.schema -ne 'mhwi-equipmentmorph-install-undo-v1' -or @($taskUndo.files).Count -ne 3){throw 'Invalid undo receipt'}
        $taskEntries=$taskUndo.files;$taskPrevious=$taskUndo.previous
        if($taskPrevious){ValidateState $taskPrevious}
    }else{$taskEntries=$taskState.originals;$taskPrevious=$null}
    $taskSeen=@{};foreach($taskEntry in $taskEntries){
        if($taskEntry.path -notin $taskAllowed -or $taskSeen.ContainsKey($taskEntry.path)){throw 'Invalid undo path'};$taskSeen[$taskEntry.path]=$true
        if($taskEntry.existed){ValidateBackup $taskEntry}
    }
    # Back up all current managed binaries before restoration, so a failed copy can be reversed.
    New-Item -ItemType Directory -Path $taskTransactionRoot | Out-Null
    $taskRescue=@();foreach($taskEntry in $taskState.files){
        $taskSavedRelative=$taskTransactionRelative+'/'+[IO.Path]::GetFileName($taskEntry.path)
        CopyManaged (Target $taskEntry.path) (Target $taskSavedRelative)
        $taskRescue+= [ordered]@{path=$taskEntry.path;existed=$true;backup=$taskSavedRelative;sha256=$taskEntry.sha256}
    }
    Exited
    try{
        RestoreEntries $taskEntries
        if($taskPrevious){WriteJson $taskStatePath $taskPrevious}else{Remove-Item -LiteralPath $taskStatePath}
    }catch{RestoreEntries $taskRescue;WriteJson $taskStatePath $taskState;throw}
    if(-not $taskPrevious -and $taskState.config_created -and (Test-Path -LiteralPath (Target $taskConfig)) -and (Hash (Target $taskConfig)) -eq $taskState.empty_config_sha256){Remove-Item -LiteralPath (Target $taskConfig)}
    WriteJson (Join-Path $taskTransactionRoot 'result.json') ([ordered]@{mode=$Mode;restored=$true;user_packages_touched=$false;previous_installation_restored=($null -ne $taskPrevious)})
    Write-Output "$Mode complete. User packages and modified configuration were preserved."
    return
}
if($Mode -eq 'Install' -and $taskState){throw 'Already managed; use Upgrade'}
if($Mode -eq 'Upgrade' -and -not $taskState){throw 'No managed installation; use Install first'}
$taskManifest=Get-Content -LiteralPath (Join-Path $taskPackageRoot 'manifest.json') -Raw | ConvertFrom-Json
if($taskManifest.schema -ne 'mhwi-equipmentmorph-runtime-distribution-v1' -or @($taskManifest.files).Count -ne 3){throw 'Invalid distribution manifest'}
$taskSeen=@{}
foreach($taskEntry in $taskManifest.files){
    if($taskEntry.path -notin $taskAllowed -or $taskSeen.ContainsKey($taskEntry.path) -or $taskEntry.sha256 -notmatch '^[0-9a-f]{64}$'){throw 'Invalid package target'};$taskSeen[$taskEntry.path]=$true
    $taskSource=Join-Path $taskPackageRoot ('payload/'+$taskEntry.path)
    if((Hash $taskSource) -ne $taskEntry.sha256){throw 'Distribution payload differs'}
    $taskDestination=Target $taskEntry.path
    if(-not $taskState -and (Test-Path -LiteralPath $taskDestination)){
        $taskAccepted=@($taskEntry.sha256)+@($taskEntry.accepted_predecessor_sha256)
        if((Hash $taskDestination) -notin $taskAccepted){throw 'Existing unmanaged file differs; no overwrite performed'}
    }
}
$taskTemplate=Join-Path $taskPackageRoot 'runtime.ini';$taskTemplateHash=Hash $taskTemplate
if($taskTemplateHash -ne $taskManifest.empty_config_sha256){throw 'Empty configuration differs'}
New-Item -ItemType Directory -Path $taskTransactionRoot | Out-Null
$taskBefore=@();foreach($taskEntry in $taskManifest.files){
    $taskDestination=Target $taskEntry.path;$taskExists=Test-Path -LiteralPath $taskDestination
    $taskSavedRelative=$taskTransactionRelative+'/'+[IO.Path]::GetFileName($taskEntry.path)
    if($taskExists){CopyManaged $taskDestination (Target $taskSavedRelative)}
    $taskBefore+=[ordered]@{path=$taskEntry.path;existed=$taskExists;backup=$(if($taskExists){$taskSavedRelative}else{$null});sha256=$(if($taskExists){Hash $taskDestination}else{$null})}
}
$taskConfigCreated=-not (Test-Path -LiteralPath (Target $taskConfig));$taskConfigCopied=$false
$taskUndo=[ordered]@{schema='mhwi-equipmentmorph-install-undo-v1';files=$taskBefore;previous=$taskState}
$taskUndoRelative=$taskTransactionRelative+'/undo.json';WriteJson (Target $taskUndoRelative) $taskUndo
Exited
try{
    foreach($taskEntry in $taskManifest.files){$taskDestination=Target $taskEntry.path;CopyManaged (Join-Path $taskPackageRoot ('payload/'+$taskEntry.path)) $taskDestination;if((Hash $taskDestination) -ne $taskEntry.sha256){throw 'Installed payload differs'}}
    if($taskConfigCreated){CopyManaged $taskTemplate (Target $taskConfig);$taskConfigCopied=$true}
    $taskNewState=[ordered]@{schema='mhwi-equipmentmorph-install-state-v1';project_name='MHWI-EquipmentMorph';version=$taskManifest.version;game_directory=$taskGameRoot;
        files=$taskManifest.files;originals=$(if($taskState){$taskState.originals}else{$taskBefore});undo=$taskUndoRelative;
        config_created=($taskConfigCreated -or ($taskState -and $taskState.config_created));empty_config_sha256=$(if($taskState){$taskState.empty_config_sha256}else{$taskTemplateHash})}
    WriteJson $taskStatePath $taskNewState
}catch{
    RestoreEntries $taskBefore
    if($taskConfigCopied -and (Hash (Target $taskConfig)) -eq $taskTemplateHash){Remove-Item -LiteralPath (Target $taskConfig)}
    if($taskState){WriteJson $taskStatePath $taskState}elseif(Test-Path -LiteralPath $taskStatePath){Remove-Item -LiteralPath $taskStatePath}
    throw
}
Write-Output "$Mode complete. Restart MHW to load the runtime; existing configuration and user packages were preserved."
} finally {if($taskLocked){$taskLock.ReleaseMutex()};$taskLock.Dispose()}
