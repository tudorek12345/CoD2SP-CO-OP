param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$BuildDirectory = '',
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $projectRoot "build\verified-x86\$Configuration" }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot "analysis\reports\network-$Configuration" }
$endpoint = Join-Path $BuildDirectory 'COD2CoopEndpoint.exe'
if (-not (Test-Path -LiteralPath $endpoint)) { throw "Build the endpoint first: $endpoint" }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$pythonExe = (Get-Command python).Source
$relayScript = Join-Path $PSScriptRoot 'udp_impairment.py'

function Get-FreePort {
    $udp = [System.Net.Sockets.UdpClient]::new([System.Net.IPEndPoint]::new([System.Net.IPAddress]::Loopback, 0))
    try { return $udp.Client.LocalEndPoint.Port } finally { $udp.Dispose() }
}
function Quote-Argument([string]$Value) { return '"' + $Value + '"' }
function Start-Hidden([string]$File, [string[]]$Arguments, [string]$LogBase) {
    return Start-Process -FilePath $File -ArgumentList $Arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput "$LogBase.stdout.log" -RedirectStandardError "$LogBase.stderr.log"
}
function Wait-Checked($Process, [string]$Name) {
    if (-not $Process.WaitForExit(12000)) { Stop-Process -Id $Process.Id -Force; throw "$Name timed out" }
    $Process.Refresh()
    if ($Process.ExitCode -ne 0) { throw "$Name exited $($Process.ExitCode)" }
}
function Invoke-EndpointPair([string]$Name, [bool]$Impaired) {
    $hostPort = Get-FreePort
    $clientTargetPort = $hostPort
    $caseDirectory = Join-Path $OutputDirectory $Name
    New-Item -ItemType Directory -Force -Path $caseDirectory | Out-Null
    $hostResult = Join-Path $caseDirectory 'host.json'
    $clientResult = Join-Path $caseDirectory 'client.json'
    $serverProcess = Start-Hidden $endpoint @('host', "$hostPort", (Quote-Argument $hostResult)) (Join-Path $caseDirectory 'host')
    $relayProcess = $null
    try {
        if ($Impaired) {
            do { $clientTargetPort = Get-FreePort } while ($clientTargetPort -eq $hostPort)
            $relayResult = Join-Path $caseDirectory 'relay.json'
            $relayProcess = Start-Hidden $pythonExe @((Quote-Argument $relayScript), "$clientTargetPort", "$hostPort", (Quote-Argument $relayResult)) (Join-Path $caseDirectory 'relay')
        }
        Start-Sleep -Milliseconds 150
        $clientProcess = Start-Hidden $endpoint @('client', "$clientTargetPort", (Quote-Argument $clientResult)) (Join-Path $caseDirectory 'client')
        Wait-Checked $clientProcess "$Name client"
        Wait-Checked $serverProcess "$Name host"
        if ($relayProcess) { Wait-Checked $relayProcess "$Name relay" }
        $hostEvidence = Get-Content -LiteralPath $hostResult -Raw | ConvertFrom-Json
        $clientEvidence = Get-Content -LiteralPath $clientResult -Raw | ConvertFrom-Json
        if (-not $hostEvidence.pass -or -not $clientEvidence.pass) { throw "$Name result reports failure" }
        if ($Impaired) {
            $relayEvidence = Get-Content -LiteralPath $relayResult -Raw | ConvertFrom-Json
            if (-not $relayEvidence.pass -or $hostEvidence.reliable_retries -lt 1 -or $clientEvidence.duplicate_events -lt 1) { throw 'Impairment retry/deduplication evidence missing' }
        }
        Write-Output "PASS $Name ($caseDirectory)"
    } finally {
        foreach ($taskProcess in @($serverProcess, $relayProcess)) {
            if ($taskProcess -and -not $taskProcess.HasExited) { Stop-Process -Id $taskProcess.Id -Force }
        }
    }
}
Invoke-EndpointPair 'direct' $false
Invoke-EndpointPair 'drop-and-duplicate' $true
Write-Output 'Fixture transport passed; this test does not exercise Call of Duty 2 entities or gameplay.'
