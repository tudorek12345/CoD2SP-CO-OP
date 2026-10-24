[CmdletBinding()]
param(
 [string]$Game='C:\Program Files (x86)\Steam\steamapps\common\Call of Duty 2',
 [string]$SteamRoot='', [Alias('Home')][string]$StageHome='',
 [string]$Assets='', [string]$Report='', [int]$HostProcessId=0,
 [ValidateRange(5,90)][int]$StartupTimeoutSeconds=35,
 [ValidateRange(0,90)][int]$WaitForHostSeconds=45
)
# Child-only environment; original wrapper authenticates against genuine Steam.
# No copied Steam DLLs, executable patches, global environment or Steam file changes.
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
if (-not $StageHome) { $StageHome=Join-Path $projectRoot 'runtime/second-instance/game-home' }
if (-not $Assets) { $Assets=Join-Path $projectRoot 'assets/original-mod-assets-only/ui' }
if (-not $Report) { $Report=Join-Path $projectRoot 'analysis/reports/second-instance-launch.json' }
$Game=[IO.Path]::GetFullPath($Game).TrimEnd('\','/')
$StageHome=[IO.Path]::GetFullPath($StageHome)
$Report=[IO.Path]::GetFullPath($Report)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Report)) | Out-Null
$utf8=New-Object Text.UTF8Encoding($false)
$result=[ordered]@{
 test='second original SP process; genuine Steam authentication; menu-only'
 pass=$false; source_executable_sha256=''; host_pid=0; second_pid=0
 game=$Game; isolated_home=$StageHome; steam_root=''
 child_environment_only=$true; steam_app_id='2630'; steam_game_id='2630'
 map_command_sent=$false; authentication_bypassed=$false; game_files_modified=$false
 requested_window=[ordered]@{client_width=640;client_height=480;r_mode='640x480';x=670;y=40;max_fps=60}
 renderer_loaded=$false; genuine_steam_dll_loaded=$false
 window_title=''; observed_client_rectangle=$null; observed_window_rectangle=$null
 exited=$false; exit_code=$null; host_still_running=$false; reason='not started'
}
$child=$null
function Write-Report {
 $json=$result | ConvertTo-Json -Depth 8
 [IO.File]::WriteAllText($Report,$json+"`n",$utf8)
 Write-Output $json
}
function Quote-ProcessArgument([string]$Value) {
 if ($Value.Contains('"') -or $Value.Contains("`r") -or $Value.Contains("`n")) { throw 'Invalid character in launch path' }
 return '"'+$Value.TrimEnd('\')+'"'
}
function Get-SupportedGameProcesses {
 @(Get-Process -Name CoD2SP_s -ErrorAction SilentlyContinue | Where-Object {
  try { [IO.Path]::GetFullPath($_.Path).Equals((Join-Path $Game 'CoD2SP_s.exe'),[StringComparison]::OrdinalIgnoreCase) }
  catch { $false }
 })
}
try {
 $exe=Join-Path $Game 'CoD2SP_s.exe'
 $hash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
 $result.source_executable_sha256=$hash
 if ($hash -ne 'ab17a89cc3722ae80edc1f4483cacbcae08b0373b9128fe4ec053ebc79062281') { throw 'Unsupported original Steam SP executable hash' }
 if (-not $SteamRoot) { $SteamRoot=(Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -Name SteamPath).SteamPath }
 $SteamRoot=[IO.Path]::GetFullPath($SteamRoot).TrimEnd('\','/')
 $result.steam_root=$SteamRoot
 $steamDll=Join-Path $SteamRoot 'Steam.dll'
 if (-not [IO.File]::Exists($steamDll) -or -not [IO.File]::Exists((Join-Path $SteamRoot 'steam.exe'))) { throw 'Genuine installed Steam root/Steam.dll not found' }
 $steamProcesses=@(Get-Process -Name steam -ErrorAction SilentlyContinue | Where-Object {
  try { [IO.Path]::GetFullPath($_.Path).Equals((Join-Path $SteamRoot 'steam.exe'),[StringComparison]::OrdinalIgnoreCase) }
  catch { $false }
 })
 if ($steamProcesses.Count -eq 0) { throw 'The genuine Steam client must already be running normally' }
 $result.steam_process_ids=@($steamProcesses | ForEach-Object { $_.Id })
 $result.installed_steam_dll_sha256=(Get-FileHash -LiteralPath $steamDll -Algorithm SHA256).Hash.ToLowerInvariant()
 $deadline=[DateTime]::UtcNow.AddSeconds($WaitForHostSeconds)
 do {
  $games=@(Get-SupportedGameProcesses)
  if ($games.Count -gt 0) { break }
  if ([DateTime]::UtcNow -ge $deadline) { throw 'No first original SP host process appeared; second instance was not started' }
  Start-Sleep -Milliseconds 500
 } while ($true)
 if ($games.Count -ne 1) { throw 'Exactly one original SP process must exist before this two-instance test' }
 if ($HostProcessId -and $HostProcessId -ne $games[0].Id) { throw 'Requested host PID does not match the existing original SP process' }
 $HostProcessId=$games[0].Id; $result.host_pid=$HostProcessId
 if ($StageHome.Equals($Game,[StringComparison]::OrdinalIgnoreCase) -or $StageHome.StartsWith($Game+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Second-instance home must be outside the Steam game installation' }
 $stageOutput=& powershell.exe -NoProfile -NonInteractive -File (Join-Path $PSScriptRoot 'Stage-NativeUi.ps1') -Game $Game -Home $StageHome -Assets $Assets 2>&1
 if ($LASTEXITCODE -ne 0) { throw ('Native UI staging failed: '+($stageOutput -join "`n")) }
 $players=Join-Path $StageHome 'cod2coop/players'; $active=Join-Path $players 'active.txt'
 if (-not [IO.File]::Exists($active)) {
  $profile=Join-Path $players 'coop_alpha';[IO.Directory]::CreateDirectory($profile) | Out-Null
  [IO.File]::WriteAllText($active,'coop_alpha',$utf8)
  [IO.File]::WriteAllText((Join-Path $profile 'config.cfg'),"// COD2 CO-OP isolated second process profile.`nseta name `"Coop Client`"`nseta r_fullscreen `"0`"`n",$utf8)
 }
 $start=New-Object Diagnostics.ProcessStartInfo
 $start.FileName=$exe; $start.WorkingDirectory=$Game
 $start.UseShellExecute=$false; $start.CreateNoWindow=$false; $start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Normal
 $start.Arguments='+set r_fullscreen 0 +set r_mode 640x480 +set vid_xpos 670 +set vid_ypos 40 +set com_maxfps 60 +set fs_homepath '+(Quote-ProcessArgument $StageHome)+' +set fs_game cod2coop +set logfile 2 +set developer 1'
 $start.EnvironmentVariables['PATH']=$SteamRoot+';'+$start.EnvironmentVariables['PATH']
 $start.EnvironmentVariables['SteamAppId']='2630'; $start.EnvironmentVariables['SteamGameId']='2630'
 $result.arguments=$start.Arguments
 $child=[Diagnostics.Process]::Start($start)
 if (-not $child) { throw 'ProcessStartInfo did not return a child process' }
 $result.second_pid=$child.Id
 if ($child.Id -eq $HostProcessId) { throw 'Unexpected process identity collision' }
 $result.started_at_utc=[DateTime]::UtcNow.ToString('o')
 $deadline=[DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
 $modules=@(); $lastModuleError=''
 do {
  $child.Refresh()
  if ($child.HasExited) { $result.exited=$true; $result.exit_code=$child.ExitCode; break }
  try {
   $modules=@($child.Modules | ForEach-Object { [ordered]@{name=$_.ModuleName;path=$_.FileName;base=('0x{0:x}' -f $_.BaseAddress.ToInt64());size=$_.ModuleMemorySize} })
   $renderer=@($modules | Where-Object { $_.name -ieq 'gfx_d3d_x86_s.dll' })
   $loadedSteam=@($modules | Where-Object { $_.name -ieq 'Steam.dll' })
   $result.renderer_loaded=$renderer.Count -eq 1
   $result.loaded_steam_dll_paths=@($loadedSteam | ForEach-Object { $_.path })
   $result.genuine_steam_dll_loaded=@($loadedSteam | Where-Object { [IO.Path]::GetFullPath($_.path).Equals($steamDll,[StringComparison]::OrdinalIgnoreCase) }).Count -eq 1
  } catch { $lastModuleError=$_.Exception.Message }
  $result.window_title=$child.MainWindowTitle
  if ($result.renderer_loaded -and $result.genuine_steam_dll_loaded -and $child.MainWindowHandle -ne [IntPtr]::Zero) { break }
  Start-Sleep -Milliseconds 250
 } while ([DateTime]::UtcNow -lt $deadline)
 $child.Refresh();$result.exited=$child.HasExited
 if ($result.exited) { $result.exit_code=$child.ExitCode }
 $result.host_still_running=@(Get-Process -Id $HostProcessId -ErrorAction SilentlyContinue).Count -eq 1
 $result.window_title=if ($result.exited) { '' } else { $child.MainWindowTitle }
 $modulePath=[IO.Path]::ChangeExtension($Report,'.modules.json')
 [IO.File]::WriteAllText($modulePath,($modules | ConvertTo-Json -Depth 5)+"`n",$utf8)
 $result.module_report=$modulePath;$result.module_enumeration_error=$lastModuleError
 if (-not $result.exited -and $child.MainWindowHandle -ne [IntPtr]::Zero) {
  $windowSource='using System; using System.Runtime.InteropServices; public static class CoopSecondInstanceWindow { [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; } [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window,out Rect rectangle); [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window,out Rect rectangle); }'
  Add-Type -TypeDefinition $windowSource
  $rect=New-Object CoopSecondInstanceWindow+Rect
  if ([CoopSecondInstanceWindow]::GetClientRect($child.MainWindowHandle,[ref]$rect)) { $result.observed_client_rectangle=[ordered]@{width=$rect.Right-$rect.Left;height=$rect.Bottom-$rect.Top} }
  if ([CoopSecondInstanceWindow]::GetWindowRect($child.MainWindowHandle,[ref]$rect)) { $result.observed_window_rectangle=[ordered]@{left=$rect.Left;top=$rect.Top;right=$rect.Right;bottom=$rect.Bottom} }
 }
 $console=Join-Path $StageHome 'cod2coop/console.log';$result.console_log=$console
 if ([IO.File]::Exists($console)) {
  $tailPath=[IO.Path]::ChangeExtension($Report,'.console-tail.log')
  [IO.File]::WriteAllText($tailPath,((@(Get-Content -LiteralPath $console) | Select-Object -Last 100) -join "`n")+"`n",$utf8)
  $result.console_tail_report=$tailPath
 }
 $result.pass=(-not $result.exited) -and $result.renderer_loaded -and $result.genuine_steam_dll_loaded -and $result.host_still_running -and ($child.MainWindowHandle -ne [IntPtr]::Zero)
 $result.reason=if ($result.pass) { 'Two original SP processes alive; second loaded genuine Steam.dll and original renderer at menu-only startup' } elseif ($result.exited) { 'Original wrapper/game exited; no licensing or instance restriction patched' } else { 'Original renderer window with genuine Steam.dll not established before timeout; inspect dialogs/logs' }
 Write-Report
 if ($result.pass) { exit 0 } else { exit 1 }
} catch {
 $result.reason=$_.Exception.Message
 if ($HostProcessId) { $result.host_still_running=@(Get-Process -Id $HostProcessId -ErrorAction SilentlyContinue).Count -eq 1 }
 Write-Report
 Write-Error ('COD2 CO-OP second instance: '+$_.Exception.Message)
 exit 1
}
