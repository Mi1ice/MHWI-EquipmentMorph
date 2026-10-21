param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$taskCatalogPath=Join-Path (Split-Path -Parent $PSScriptRoot) 'blender_addon_v044/mhwi_equipmentmorph/equipment_catalog.json'
$taskCatalog=Get-Content -LiteralPath $taskCatalogPath -Raw | ConvertFrom-Json
if($taskCatalog.schema -ne 'mhwi-equipment-catalogue-v2'){throw 'Unsupported equipment catalogue'}
$taskNames=@{}
foreach($taskSet in $taskCatalog.sets){
    $taskName=[string]$taskSet.name
    if($taskName -match '^【(.+)】服装$'){$taskName=$Matches[1]}
    $taskNames[[int]$taskSet.series_id]=$taskName
}
function Get-CppLiteral([string]$Text){
    if($Text -match '[\x00-\x1f]'){throw 'Equipment label contains a control character'}
    '"'+$Text.Replace('\','\\').Replace('"','\"')+'"'
}
$taskLines=[Collections.Generic.List[string]]::new()
$taskLines.Add('// Generated from the bundled equipment catalogue; display labels only.')
$taskLines.Add('#pragma once')
$taskLines.Add('#include <string_view>')
$taskLines.Add('namespace morphequipment {')
$taskLines.Add('struct EquipmentLabel {std::string_view model,name;};')
$taskLines.Add('inline constexpr EquipmentLabel labels[]={')
$taskPrevious='';$taskCount=0;$taskNamed=0
foreach($taskModel in $taskCatalog.model_resource_index){
    $taskModelId=[string]$taskModel.model_id
    if($taskModelId -notmatch '^pl/[fm]_equip/pl\d{3}_\d{4}/(helm|body|arm|wst|leg)/mod/[fm]_\w+$' -or ($taskPrevious -and [string]::CompareOrdinal($taskPrevious,$taskModelId) -ge 0)){throw 'Invalid or unordered equipment resource index'}
    $taskCandidates=@($taskModel.candidates | ForEach-Object {$taskNames[[int]$_.series_id]} | Sort-Object -Unique)
    $taskLabel=if($taskCandidates.Count -eq 1){[string]$taskCandidates[0]}else{''}
    if($taskLabel){$taskNamed++}
    $taskLines.Add('    {'+(Get-CppLiteral $taskModelId)+','+(Get-CppLiteral $taskLabel)+'},')
    $taskPrevious=$taskModelId;$taskCount++
}
if($taskCount -ne $taskCatalog.model_resource_index.Count -or $taskCount -lt 1){throw 'Empty equipment resource index'}
$taskLines.Add('};')
$taskLines.Add('}')
[IO.File]::WriteAllLines([IO.Path]::GetFullPath($Output),$taskLines,[Text.UTF8Encoding]::new($false))
[pscustomobject]@{models=$taskCount;named=$taskNamed;catalog_sha256=(Get-FileHash -LiteralPath $taskCatalogPath -Algorithm SHA256).Hash.ToLowerInvariant()}
