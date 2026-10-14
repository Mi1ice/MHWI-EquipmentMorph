[CmdletBinding(DefaultParameterSetName='Export')]
param(
    [Parameter(Mandatory,ParameterSetName='Export')][string]$ExportDirectory,
    [Parameter(Mandatory,ParameterSetName='Installed')][string]$GameDirectory,
    [string]$Validator,
    [Parameter(ParameterSetName='Export')][string]$RuntimeConfig
)
$ErrorActionPreference='Stop'
if(-not $Validator){
    $Validator=Join-Path $PSScriptRoot 'EquipmentMorphPackageValidator.exe'
    if(-not(Test-Path -LiteralPath $Validator)){$Validator=Join-Path (Split-Path -Parent $PSScriptRoot) '.build/current-validation/package-validator/EquipmentMorphPackageValidator.exe'}
}
if(-not(Test-Path -LiteralPath $Validator)){throw 'Native validator missing; run build_equipment_morph_package_validator.ps1'}
if($PSCmdlet.ParameterSetName -eq 'Export'){
    $taskModels=Join-Path $ExportDirectory 'nativePC'
    if($RuntimeConfig){
        $taskData=$RuntimeConfig
        if([IO.Path]::GetFileName($taskData) -ieq 'runtime.ini'){$taskData=Split-Path -Parent $taskData}
    }else{
        $taskProject=Split-Path -Parent $PSScriptRoot
        $taskConfig=Join-Path $taskProject 'config/mhwi-equipmentmorph-current.json'
        if(-not(Test-Path -LiteralPath $taskConfig)){throw '请通过 -RuntimeConfig 指定运行时解压目录或已安装的配置目录'}
        $taskCurrent=Get-Content -LiteralPath $taskConfig -Raw|ConvertFrom-Json
        $taskData=Join-Path $taskProject $taskCurrent.release_directory
    }
}else{
    if(-not(Test-Path -LiteralPath (Join-Path $GameDirectory 'MonsterHunterWorld.exe'))){throw 'MHW executable not found in chosen directory'}
    $taskData=Join-Path $GameDirectory 'nativePC/plugins/MHWEquipmentMorph';$taskModels=Join-Path $GameDirectory 'nativePC'
}
if(-not(Test-Path -LiteralPath (Join-Path $taskData 'runtime.ini'))){throw '未找到运行时配置，请先安装运行时或指定正确的 -RuntimeConfig。MOD本身不携带此配置。'}
$taskText=& $Validator $taskData $taskModels
$taskExit=$LASTEXITCODE
$taskReport=($taskText -join "`n")|ConvertFrom-Json
if($taskExit -ne 0 -or -not $taskReport.native_loader_valid){throw ('Package/model validation failed: '+$taskReport.error)}
$taskReport|ConvertTo-Json -Depth 6
