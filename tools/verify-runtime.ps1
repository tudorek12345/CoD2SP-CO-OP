param([Parameter(Mandatory=$true)][string]$LogPath,[string]$OutputPath='analysis/reports/native-runtime-acceptance.json')
$ErrorActionPreference='Stop'
$records=@(Get-Content -LiteralPath $LogPath | ForEach-Object {ConvertFrom-Json $_})
function Has-Evidence([string]$Event,[string]$Pattern){return @($records | Where-Object {$_.event -eq $Event -and $_.detail -match $Pattern}).Count -gt 0}
$observations=@($records | Where-Object {$_.event -eq 'native_player_observation' -and $_.detail -match 'valid=1'})
$positions=@($observations | ForEach-Object {($_.detail -split 'origin=')[1]} | Sort-Object -Unique)
$result=[ordered]@{
    test='native SP integration; one original player, no cooperative gameplay claim'
    executable_hash_verified=(Has-Evidence 'compatibility' '^ab17a89cc3722ae80edc1f4483cacbcae08b0373b9128fe4ec053ebc79062281$')
    command_callback_executed=(Has-Evidence 'engine_command_callback' 'verified=1 count=1')
    moscow_observed=(Has-Evidence 'observed_mission' '^moscow$')
    one_client_capacity=(Has-Evidence 'native_player_observation' 'valid=1 .*client_capacity=1 client_backed_entities=1')
    observed_positions=$positions.Count
    game_player_position_changed=($positions.Count -gt 1)
    external_peer_connected=(Has-Evidence 'network' 'connected')
    peer_disconnected=(Has-Evidence 'network' 'peer disconnected')
    command_removed_on_game_thread=(Has-Evidence 'engine_command_cleanup' '^removed on game thread$')
    hook_shutdown=(Has-Evidence 'shutdown' 'Hook removed')
    source_log=(Resolve-Path -LiteralPath $LogPath).Path
}
$result['pass']=$result.executable_hash_verified -and $result.command_callback_executed -and $result.moscow_observed -and $result.one_client_capacity -and $result.external_peer_connected -and $result.peer_disconnected -and $result.command_removed_on_game_thread -and $result.hook_shutdown
$result | ConvertTo-Json | Set-Content -LiteralPath $OutputPath
if(-not $result.pass){throw "Native acceptance evidence incomplete. Inspect $OutputPath"}
Write-Output "PASS native runtime integration evidence ($($positions.Count) distinct real host positions); co-op player lifecycle unavailable."
