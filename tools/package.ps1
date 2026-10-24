param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$package=Join-Path $projectRoot 'dist/COD2-Coop-0.1.0-alpha'
New-Item -ItemType Directory -Force -Path $package | Out-Null
cmake --install "$projectRoot/build/verified-x86" --config $Configuration --prefix $package
if($LASTEXITCODE){throw 'Install failed'}
Copy-Item -LiteralPath "$projectRoot/README.md" -Destination $package
Compress-Archive -Path "$package/*" -DestinationPath "$projectRoot/dist/COD2-Coop-0.1.0-alpha.zip" -Force
Get-ChildItem -LiteralPath $package -Recurse -File | Get-FileHash -Algorithm SHA256 | Select-Object Path,Hash | ConvertTo-Json | Set-Content -LiteralPath "$projectRoot/dist/package-sha256.json"
