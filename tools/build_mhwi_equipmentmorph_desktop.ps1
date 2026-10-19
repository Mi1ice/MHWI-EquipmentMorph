[CmdletBinding()]
param([switch]$SkipInstall,[switch]$CheckOnly)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskDesktop=Join-Path $taskRoot 'controls/MHWI-EquipmentMorph-Desktop'
$taskCargo=Get-Command cargo -ErrorAction SilentlyContinue
if(-not $taskCargo){
    $taskDependencies=Join-Path $taskRoot '.build/tauri-dependencies'
    $taskExecutable=Join-Path $taskDependencies 'cargo/bin/cargo.exe'
    if(-not (Test-Path -LiteralPath $taskExecutable)){throw 'Install stable Rust (MSVC), or provide the project-local Rust toolchain.'}
    $env:RUSTUP_HOME=Join-Path $taskDependencies 'rustup'
    $env:CARGO_HOME=Join-Path $taskDependencies 'cargo'
    $env:PATH=(Join-Path $env:CARGO_HOME 'bin')+';'+$env:PATH
}
$env:CARGO_TARGET_DIR=Join-Path $taskRoot '.build/control-center-target'
Push-Location $taskDesktop
try{
    if(-not $SkipInstall){& npm.cmd ci --no-audit --no-fund;if($LASTEXITCODE -ne 0){throw 'Desktop dependency installation failed'}}
    if($CheckOnly){
        & npm.cmd run build;if($LASTEXITCODE -ne 0){throw 'Frontend build failed'}
        & cargo test --locked --manifest-path src-tauri/Cargo.toml
        if($LASTEXITCODE -ne 0){throw 'Desktop backend checks failed'}
    }else{
        & npm.cmd run tauri -- build --no-bundle --ci
        if($LASTEXITCODE -ne 0){throw 'Tauri desktop build failed'}
        $taskOutput=Join-Path $taskRoot '.build/current-validation/desktop'
        New-Item -ItemType Directory -Path $taskOutput -Force|Out-Null
        Copy-Item -LiteralPath (Join-Path $env:CARGO_TARGET_DIR 'release/MHWI-EquipmentMorph-ControlCenter.exe') -Destination $taskOutput -Force
        & cargo metadata --locked --offline --filter-platform x86_64-pc-windows-msvc --format-version 1 --manifest-path src-tauri/Cargo.toml | Set-Content -LiteralPath (Join-Path $taskOutput 'cargo-metadata.json') -Encoding UTF8
        if($LASTEXITCODE -ne 0){throw 'Dependency metadata collection failed'}
        Write-Output ('PASS: Tauri control center built at '+$taskOutput)
    }
}finally{Pop-Location}
