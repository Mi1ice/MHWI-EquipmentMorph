$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'windows-toolchain.ps1')
$taskChain=Get-EquipmentMorphToolchain
$taskOutput=Join-Path $taskRoot '.build/author-shading-native'
New-Item -ItemType Directory -Force -Path $taskOutput | Out-Null
$taskUpstream=Join-Path $taskRoot 'research/blender-mikktspace-5.1.2'
$taskPinned=Get-Content (Join-Path $taskUpstream 'UPSTREAM.json') -Raw | ConvertFrom-Json
foreach($taskEntry in $taskPinned.sources.PSObject.Properties){
    if((Get-FileHash -LiteralPath (Join-Path $taskUpstream $taskEntry.Name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskEntry.Value){throw ('MikkTSpace source digest mismatch: '+$taskEntry.Name)}
}
$taskDll=Join-Path $taskOutput 'MhmAuthorShading.dll'
$taskArgs=@('/nologo','/utf-8','/MT','/O2','/std:c++20','/EHsc','/LD','/fp:strict','/DNDEBUG',
    ('/I'+(Join-Path $taskChain.CompilerRoot 'include')),('/I'+(Join-Path $taskChain.SdkRoot ('Include/'+$taskChain.SdkVersion+'/ucrt'))),
    (Join-Path $taskRoot 'native/author_shading.cpp'),('/Fo'+$taskOutput+'\'),('/Fe'+$taskDll),'/link','/NOLOGO','/Brepro',
    ('/LIBPATH:'+(Join-Path $taskChain.CompilerRoot 'lib/x64')),('/LIBPATH:'+(Join-Path $taskChain.SdkRoot ('Lib/'+$taskChain.SdkVersion+'/ucrt/x64'))),
    ('/LIBPATH:'+(Join-Path $taskChain.SdkRoot ('Lib/'+$taskChain.SdkVersion+'/um/x64'))))
& (Join-Path $taskChain.CompilerRoot 'bin/Hostx64/x64/cl.exe') @taskArgs
if($LASTEXITCODE -ne 0){throw 'Author shading helper build failed'}
$taskHash=(Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash.ToLowerInvariant()
$taskNative=Join-Path $taskRoot 'blender_addon_v044/mhwi_equipmentmorph/_native/build'
New-Item -ItemType Directory -Force -Path $taskNative | Out-Null
$taskName='mhm_author_shading-'+$taskHash+'.dll'
$taskDestination=Join-Path $taskNative $taskName
if(-not (Test-Path -LiteralPath $taskDestination)){Copy-Item -LiteralPath $taskDll -Destination $taskDestination}
elseif((Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskHash){throw 'Existing author helper digest mismatch'}
$taskSources=@{'native/author_shading.cpp'=(Get-FileHash -LiteralPath (Join-Path $taskRoot 'native/author_shading.cpp') -Algorithm SHA256).Hash.ToLowerInvariant()}
foreach($taskEntry in $taskPinned.sources.PSObject.Properties){$taskSources['research/blender-mikktspace-5.1.2/'+$taskEntry.Name]=$taskEntry.Value}
$taskManifest=@{schema='mhwi-author-shading-native-v1';abi=1;file=$taskName;sha256=$taskHash;platform='windows-x64';blender='5.1.2';algorithm='Blender-MikkTSpace-serial';source_sha256=$taskSources}
$taskManifest.license_sha256=@{}
foreach($taskName in @('LICENSE.Apache-2.0.txt','NOTICE.txt')){$taskManifest.license_sha256[$taskName]=(Get-FileHash -LiteralPath (Join-Path $taskUpstream $taskName) -Algorithm SHA256).Hash.ToLowerInvariant()}
$taskManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskNative 'build.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $taskUpstream 'LICENSE.Apache-2.0.txt') -Destination (Join-Path $taskNative 'LICENSE.Apache-2.0.txt')
Copy-Item -LiteralPath (Join-Path $taskUpstream 'NOTICE.txt') -Destination (Join-Path $taskNative 'NOTICE.txt')
Write-Host ('PASS: author shading helper '+$taskHash+'; no game deployment')
