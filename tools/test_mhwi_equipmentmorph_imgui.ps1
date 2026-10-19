param([string]$BuildDirectory,[string]$OutputDirectory)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskBuild=if($BuildDirectory){[IO.Path]::GetFullPath($BuildDirectory)}else{Join-Path $taskRoot '.build/current-validation/imgui'}
$taskOutput=if($OutputDirectory){[IO.Path]::GetFullPath($OutputDirectory)}else{Join-Path $taskRoot ('.build/imgui-binding-tests-'+[guid]::NewGuid().ToString('N'))}
if(Test-Path -LiteralPath $taskOutput){throw 'Use a new output directory to preserve previous test evidence'}
New-Item -ItemType Directory -Path $taskOutput|Out-Null
$taskResults=@()
foreach($taskMode in @('early','late')){
    $taskDirectory=Join-Path $taskOutput $taskMode
    New-Item -ItemType Directory -Path $taskDirectory|Out-Null
    foreach($taskFile in @('EquipmentMorphImGuiFixture.exe','MHWEquipmentMorphImGui.dll')){Copy-Item -LiteralPath (Join-Path $taskBuild $taskFile) -Destination $taskDirectory}
    $taskArguments=@('--smoke');if($taskMode -eq 'late'){$taskArguments+='--late-load'}
    $taskProcess=Start-Process -FilePath (Join-Path $taskDirectory EquipmentMorphImGuiFixture.exe) -ArgumentList $taskArguments -WorkingDirectory $taskDirectory -WindowStyle Hidden -PassThru
    if(-not $taskProcess.WaitForExit(30000)){throw ('Fixture timed out; inspect PID '+$taskProcess.Id)}
    if($taskProcess.ExitCode -ne 0){throw ('Fixture failed: '+$taskMode+' exit '+$taskProcess.ExitCode)}
    $taskFixture=Get-Content -LiteralPath (Join-Path $taskDirectory fixture-state.json) -Raw|ConvertFrom-Json
    $taskLog=Join-Path $taskDirectory ('nativePC/plugins/MHWEquipmentMorph/logs/imgui-'+$taskProcess.Id+'.ndjson')
    $taskSamples=@(Get-Content -LiteralPath $taskLog|ForEach-Object{$_|ConvertFrom-Json}|Where-Object{$_.event -eq 'imgui_status'})
    $taskLast=$taskSamples[-1];$taskRoute=if($taskMode -eq 'late'){'backbuffer_submission'}else{'creation'}
    if(-not $taskLast.window_ready -or -not $taskLast.queue_ready -or $taskLast.error -ne 'none' -or $taskLast.binding_route -ne $taskRoute){throw ('Window/queue association failed: '+$taskMode)}
    if($taskLast.queue_address -ne $taskFixture.render_queue_address -or $taskLast.queue_address -eq $taskFixture.unrelated_queue_address){throw 'Unrelated queue was selected'}
    if($taskFixture.resizes -lt 1 -or ($taskMode -eq 'late' -and (-not $taskFixture.reset_probe -or $taskLast.matched_submissions -lt 2))){throw 'Reset/resize re-acquisition was not exercised'}
    if($taskLast.drawn -ne 0 -or $taskFixture.d3d12_errors -ne 0){throw 'Unexpected hidden draw or D3D12 error'}
    $taskResults+=@{mode=$taskMode;passed=$true;fixture=$taskFixture;last_gui_status=$taskLast}
}
[IO.File]::WriteAllText((Join-Path $taskOutput results.json),($taskResults|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Write-Output ('PASS: early/late loading, real backbuffer queue selection and hidden drawing; evidence '+$taskOutput)
