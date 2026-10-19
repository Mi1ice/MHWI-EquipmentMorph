[CmdletBinding()]
param([string]$Python = 'python', [string]$Blender, [switch]$Desktop)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
function Run([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw ('Check failed: ' + $Program) }
}
Push-Location $taskRoot
try {
    Run $Python @('tools/check_repository.py')
    Run $Python @('tools/test_equipment_catalog.py')
    Run $Python @('tools/test_equipment_morph_process_guard.py')
    & (Join-Path $PSScriptRoot 'build_mhwi_equipmentmorph_current.ps1')
    & (Join-Path $PSScriptRoot 'build_equipment_morph_package_validator.ps1')
    Run $Python @('tools/test_mhwi_equipmentmorph_authoring_clean.py', 'blender_addon_v044/mhwi_equipmentmorph')
    Run (Join-Path $taskRoot '.build/cleanup-validation/native/EquipmentMorphDiskContractTests.exe') @('.build/cleanup-validation/synthetic.mhmprofile')
    Run $Python @('tools/test_mhwi_equipmentmorph_weight_log.py')
    & (Join-Path $PSScriptRoot 'build_mhwi_equipmentmorph_control_panel.ps1') -OutputDirectory (Join-Path $taskRoot '.build/current-validation/control-panel')
    & (Join-Path $PSScriptRoot 'build_mhwi_equipmentmorph_sdk_v04.ps1') -OutputDirectory (Join-Path $taskRoot '.build/current-validation/sdk')
    if ($Blender) {
        Run $Blender @('--background', '--factory-startup', '--python-exit-code', '1', '--python', 'tools/test_mhwi_equipmentmorph_direct_blender.py')
        Run $Blender @('--background', '--factory-startup', '--python-exit-code', '1', '--python', 'tools/test_key_selection_preview.py')
    }
    if($Desktop){
        & (Join-Path $PSScriptRoot 'build_mhwi_equipmentmorph_desktop.ps1') -CheckOnly
        & (Join-Path $PSScriptRoot 'build_mhwi_equipmentmorph_desktop.ps1') -SkipInstall
        Run $Python @('tools/package_source.py','--desktop')
        & (Join-Path $PSScriptRoot 'test_mhwi_equipmentmorph_candidate_installation.ps1') -PackageDirectory (Join-Path $taskRoot '.build/packages/MHWI-EquipmentMorph-0.5.1-dev12-controlcenter-dev1')
    }else{
        Run $Python @('tools/package_source.py')
        & (Join-Path $PSScriptRoot 'test_mhwi_equipmentmorph_candidate_installation.ps1')
    }
    $taskInstallation = Get-Content -LiteralPath '.build/current-validation/installation-checks.json' -Raw | ConvertFrom-Json
    $taskExpected=if($Desktop){39}else{29}
    if ($taskInstallation.passed -ne $taskExpected) { throw 'Installer checks incomplete' }
    Write-Output 'PASS: source build, native/authoring/SDK/control checks, packaging and installer fixtures'
    if (-not $Blender) { Write-Output 'Blender paired export checks skipped; supply -Blender to include them.' }
} finally { Pop-Location }
