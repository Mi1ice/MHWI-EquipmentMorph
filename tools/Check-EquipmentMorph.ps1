[CmdletBinding(DefaultParameterSetName='Export')]
param(
    [Parameter(Mandatory,ParameterSetName='Export')][string]$ExportDirectory,
    [Parameter(Mandatory,ParameterSetName='Installed')][string]$GameDirectory,
    [string]$Validator
)
$ErrorActionPreference='Stop'
if(-not $Validator){
    $Validator=Join-Path $PSScriptRoot 'EquipmentMorphPackageValidator.exe'
    if(-not(Test-Path -LiteralPath $Validator)){$Validator=Join-Path (Split-Path -Parent $PSScriptRoot) '.build/current-validation/package-validator/EquipmentMorphPackageValidator.exe'}
}
if(-not(Test-Path -LiteralPath $Validator)){throw 'Native validator missing; run build_equipment_morph_package_validator.ps1'}
if($PSCmdlet.ParameterSetName -eq 'Export'){
    $taskData=Join-Path $ExportDirectory 'nativePC/plugins/MHWEquipmentMorph';$taskModels=Join-Path $ExportDirectory 'nativePC'
}else{
    if(-not(Test-Path -LiteralPath (Join-Path $GameDirectory 'MonsterHunterWorld.exe'))){throw 'MHW executable not found in chosen directory'}
    $taskData=Join-Path $GameDirectory 'nativePC/plugins/MHWEquipmentMorph';$taskModels=Join-Path $GameDirectory 'nativePC'
}
if(-not(Test-Path -LiteralPath (Join-Path $taskData 'runtime.ini'))){throw ('未找到形态键配置：'+(Join-Path $taskData 'runtime.ini')+'。请先安装本次导出的包并配置对应部位。')}
$taskText=& $Validator $taskData $taskModels
$taskExit=$LASTEXITCODE
$taskReport=($taskText -join "`n")|ConvertFrom-Json
if($taskExit -ne 0 -or -not $taskReport.native_loader_valid){throw ('Package/model validation failed: '+$taskReport.error)}
$taskReport|ConvertTo-Json -Depth 6
