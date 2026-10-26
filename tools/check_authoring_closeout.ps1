param(
    [ValidateSet('Entrypoints','Preflight','Export','All')][string]$Stage='All',
    [string]$Python='python', [string]$Blender,
    [string]$OutputDirectory
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if(-not $OutputDirectory){$OutputDirectory=Join-Path $taskRoot '.build/authoring-closeout'}
& $Python (Join-Path $PSScriptRoot 'authoring_closeout_report.py') --stage $Stage --blender $Blender --output $OutputDirectory
if($LASTEXITCODE -ne 0){throw "Authoring $Stage checks failed; inspect $OutputDirectory"}
