param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
cmake -S $projectRoot -B "$projectRoot/build/verified-x86" -G 'Visual Studio 17 2022' -A Win32
if($LASTEXITCODE){throw 'CMake configure failed'}
cmake --build "$projectRoot/build/verified-x86" --config $Configuration --parallel
if($LASTEXITCODE){throw 'Native build failed'}
ctest --test-dir "$projectRoot/build/verified-x86" -C $Configuration --output-on-failure
if($LASTEXITCODE){throw 'Automated tests failed'}
