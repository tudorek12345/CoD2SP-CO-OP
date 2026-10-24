[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Game,
    [Parameter(Mandatory=$true)][Alias('Home')][string]$StageHome,
    [Parameter(Mandatory=$true)][string]$Assets,
    [switch]$DryRun,
    [switch]$Remove
)
# Windows PowerShell 5.1 / built-in .NET only. Never writes the Steam installation.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$expected = [ordered]@{
    'ui/main.menu' = '5944b297bf4d1aa06836269c3f06aa14402646cfe4d73660354692a5b00b748f'
    'ui/menus.txt' = '2db6349c8802b2cd5d9556086b47b66f16b0ba38cce863ae3be2a5b92b51a7e9'
}
$gameRoot = [IO.Path]::GetFullPath($Game).TrimEnd('\','/')
$homeRoot = [IO.Path]::GetFullPath($StageHome).TrimEnd('\','/')
$stageRoot = [IO.Path]::GetFullPath((Join-Path $homeRoot 'cod2coop'))
$manifestPath = Join-Path $stageRoot 'coop-ui-manifest.json'
$generator = 'COD2 CO-OP native UI 0.1.0'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$encoding = [Text.Encoding]::GetEncoding(1252)

function Get-BytesHash([byte[]]$Bytes) {
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($hasher.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant() }
    finally { $hasher.Dispose() }
}
function Assert-SafeOutput([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if ($full.Equals($gameRoot,[StringComparison]::OrdinalIgnoreCase) -or $full.StartsWith($gameRoot+'\',[StringComparison]::OrdinalIgnoreCase)) {
        throw 'UI output must be outside the original Steam installation'
    }
    if (-not $full.Equals($stageRoot,[StringComparison]::OrdinalIgnoreCase) -and -not $full.StartsWith($stageRoot+'\',[StringComparison]::OrdinalIgnoreCase)) {
        throw "Output leaves the isolated staging directory: $full"
    }
    $current = $full
    while ($current -and $current.Length -ge $homeRoot.Length) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Refusing reparse-point output path: $current" }
        }
        $next = [IO.Path]::GetDirectoryName($current)
        if ($next -eq $current) { break }
        $current = $next
    }
}
function Get-MenuTokens([string]$Text) {
    $pattern = '//[^\r\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|[A-Za-z_][A-Za-z0-9_]*|[{}]'
    foreach ($match in [regex]::Matches($Text,$pattern,[Text.RegularExpressions.RegexOptions]::Singleline)) {
        if ($match.Value.StartsWith('//') -or $match.Value.StartsWith('/*')) { continue }
        [pscustomobject]@{ Token=$match.Value; Start=$match.Index; End=$match.Index+$match.Length }
    }
}
function Get-ClosingBrace($Tokens,[int]$Opening) {
    if ($Tokens[$Opening].Token -ne '{') { throw 'Expected opening menu brace' }
    $depth=0
    for ($i=$Opening; $i -lt $Tokens.Count; $i++) {
        if ($Tokens[$i].Token -eq '{') { $depth++ }
        elseif ($Tokens[$i].Token -eq '}') { $depth--; if ($depth -eq 0) { return $i } }
    }
    throw 'Unbalanced menu braces'
}
function Get-MenuBlock([string]$Text,[string]$Name) {
    $tokens = @(Get-MenuTokens $Text); $found=@()
    for ($i=0; $i -lt $tokens.Count-2; $i++) {
        if ($tokens[$i].Token -ine 'menuDef' -or $tokens[$i+1].Token -ne '{') { continue }
        $end = Get-ClosingBrace $tokens ($i+1)
        for ($j=$i+2; $j -lt [Math]::Min($end,$i+15); $j++) {
            if ($tokens[$j].Token -ieq 'name' -and $tokens[$j+1].Token.Trim('"') -eq $Name) {
                $found += [pscustomobject]@{ Opening=$tokens[$i+1].Start; Closing=$tokens[$end].Start }; break
            }
        }
    }
    if ($found.Count -ne 1) { throw "Expected exactly one menuDef named $Name; found $($found.Count)" }
    return $found[0]
}
function Write-AtomicBytes([string]$Path,[byte[]]$Bytes) {
    Assert-SafeOutput $Path
    $directory = [IO.Path]::GetDirectoryName($Path)
    [IO.Directory]::CreateDirectory($directory) | Out-Null
    $temp = Join-Path $directory ([IO.Path]::GetFileName($Path)+'.'+[Guid]::NewGuid().ToString('N')+'.tmp')
    $backup = $temp+'.previous'
    try {
        [IO.File]::WriteAllBytes($temp,$Bytes)
        if ([IO.File]::Exists($Path)) { [IO.File]::Replace($temp,$Path,$backup) }
        else { [IO.File]::Move($temp,$Path) }
    } finally {
        if ([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) }
        if ([IO.File]::Exists($backup)) { [IO.File]::Delete($backup) }
    }
}

try {
    Assert-SafeOutput $stageRoot
    $prior=$null
    if ([IO.File]::Exists($manifestPath)) {
        Assert-SafeOutput $manifestPath
        $prior = [IO.File]::ReadAllText($manifestPath,[Text.Encoding]::UTF8) | ConvertFrom-Json
        if ($prior.generator -ne $generator) { throw 'Existing UI manifest is not owned by this staging tool' }
    }
    $names=@('ui/main.menu','ui/menus.txt','ui/coop.menu')
    if ($Remove) {
        if (-not $prior) { throw 'No owned UI staging manifest found' }
        $oldNames=@($prior.generated.PSObject.Properties.Name)
        if ($oldNames.Count -ne 3 -or @($oldNames | Where-Object { $_ -notin $names }).Count -ne 0) { throw 'Unexpected generated-file list in manifest' }
        foreach ($name in $names) {
            $path=Join-Path $stageRoot $name; Assert-SafeOutput $path
            if ([IO.File]::Exists($path) -and (Get-BytesHash ([IO.File]::ReadAllBytes($path))) -ne $prior.generated.PSObject.Properties[$name].Value.sha256) { throw "Refusing to delete an edited staged menu: $path" }
        }
        if (-not $DryRun) {
            foreach ($name in $names) { $path=Join-Path $stageRoot $name; if ([IO.File]::Exists($path)) { [IO.File]::Delete($path) } }
            [IO.File]::Delete($manifestPath)
        }
        [ordered]@{ removed=(-not $DryRun); dry_run=[bool]$DryRun; stage=$stageRoot; files=$names } | ConvertTo-Json -Depth 5
        exit 0
    }
    $originals=@{}; $origins=[ordered]@{}
    $archives=@(Get-ChildItem -LiteralPath (Join-Path $gameRoot 'main') -Filter '*.iwd' -File | Sort-Object Name)
    if ($archives.Count -eq 0) { throw 'No installed main/*.iwd archives found' }
    foreach ($archive in $archives) {
        $zip=[IO.Compression.ZipFile]::OpenRead($archive.FullName)
        try {
            foreach ($name in $expected.Keys) {
                $entry=$zip.GetEntry($name); if (-not $entry) { continue }
                if ($originals.ContainsKey($name)) { throw "Ambiguous original UI: $name appears in more than one archive" }
                if ($entry.Length -gt 524288) { throw "Unexpected original menu size: $name" }
                $stream=$entry.Open(); $memory=New-Object IO.MemoryStream
                try { $stream.CopyTo($memory); $data=$memory.ToArray() }
                finally { $stream.Dispose(); $memory.Dispose() }
                $hash=Get-BytesHash $data
                if ($hash -ne $expected[$name]) { throw "Unsupported original $name SHA-256 $hash" }
                $originals[$name]=$data; $origins[$name]=[ordered]@{ archive=$archive.Name; sha256=$hash; bytes=$data.Length }
            }
        } finally { $zip.Dispose() }
    }
    foreach ($name in $expected.Keys) { if (-not $originals.ContainsKey($name)) { throw "Original menu missing: $name" } }
    $main=$encoding.GetString($originals['ui/main.menu'])
    $loader=$encoding.GetString($originals['ui/menus.txt'])
    $entryText=[IO.File]::ReadAllText((Join-Path $Assets 'coop_entry.menu'),[Text.Encoding]::ASCII).Replace("`r`n","`n").Replace("`r","`n")
    $own=[IO.File]::ReadAllBytes((Join-Path $Assets 'coop.menu'))
    $ownText=[Text.Encoding]::ASCII.GetString($own)
    foreach ($name in @('coop_main')) { Get-MenuBlock $ownText $name | Out-Null }
    if (-not $entryText.Contains('cod2coop_main_entry') -or -not $entryText.Contains('open coop_main')) { throw 'Independent entry asset lacks the co-op action' }
    if ($main.Contains('cod2coop_main_entry') -or $loader.Contains('"ui/coop.menu"')) { throw 'Original UI already contains co-op anchors unexpectedly' }
    $block=Get-MenuBlock $main 'main_text'
    $main=$main.Insert($block.Closing,"`r`n// COD2 CO-OP native UI generated locally`r`n"+$entryText+"`r`n")
    $tokens=@(Get-MenuTokens $loader)
    if ($tokens.Count -eq 0 -or $tokens[0].Token -ne '{') { throw 'Original menus.txt root is not recognized' }
    $end=Get-ClosingBrace $tokens 0
    if ($end -ne $tokens.Count-1) { throw 'Unexpected extra root content in original menus.txt' }
    $loader=$loader.Insert($tokens[$end].Start,"`r`n    // COD2 CO-OP native UI generated locally`r`n    loadMenu { `"ui/coop.menu`" }`r`n")
    $files=[ordered]@{ 'ui/main.menu'=$encoding.GetBytes($main); 'ui/menus.txt'=$encoding.GetBytes($loader); 'ui/coop.menu'=$own }
    $generated=[ordered]@{}
    foreach ($name in $files.Keys) {
        $path=Join-Path $stageRoot $name; Assert-SafeOutput $path
        $hash=Get-BytesHash $files[$name]
        if ([IO.File]::Exists($path)) {
            $current=Get-BytesHash ([IO.File]::ReadAllBytes($path)); $oldHash=$null
            if ($prior -and $prior.generated.PSObject.Properties[$name]) { $oldHash=$prior.generated.PSObject.Properties[$name].Value.sha256 }
            if ($current -ne $hash -and $current -ne $oldHash) { throw "Refusing to overwrite unrelated or edited UI override: $path" }
        }
        $generated[$name]=[ordered]@{ sha256=$hash; bytes=$files[$name].Length; contains_original_game_content=($name -ne 'ui/coop.menu') }
    }
    $manifest=[ordered]@{
        generator=$generator; branding='COD2 CO-OP - ALPHA 0.1.0; Developed by Heperkoo'
        game=$gameRoot; home=$homeRoot; stage=$stageRoot; originals=$origins; generated=$generated
        menu_names=@('coop_main'); native_tabs=@('main','host','join','soldier','settings')
        runtime_commands_required=@('coop_host','coop_join','coop_start','coop_disconnect','coop_soldier')
        dry_run=[bool]$DryRun
        warning='Generated main.menu and menus.txt contain installed game content; exclude them from redistribution.'
    }
    $json=$manifest | ConvertTo-Json -Depth 8
    if (-not $DryRun) {
        foreach ($name in $files.Keys) { Write-AtomicBytes (Join-Path $stageRoot $name) $files[$name] }
        Write-AtomicBytes $manifestPath ($utf8.GetBytes($json+"`n"))
    }
    Write-Output $json
    exit 0
} catch {
    Write-Error ("COD2 CO-OP UI staging: "+$_.Exception.Message)
    exit 1
}
