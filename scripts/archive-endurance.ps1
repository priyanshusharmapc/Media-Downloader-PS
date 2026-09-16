#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackageRoot,
    [Parameter(Mandatory=$true)][string]$ArchiveRoot,
    [Parameter(Mandatory=$true)][string]$PlaylistUrl,
    [Parameter(Mandatory=$true)][string]$VideoUrl,
    [int]$DurationMinutes = 60,
    [string]$PlanPath,
    [string]$BasePackageRoot,
    [string]$ExpectedCommit,
    [string]$ExpectedCiRun,
    [switch]$SkipPreflight,
    [string]$OutputJson
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if($DurationMinutes -lt 60){throw 'Endurance qualification requires at least 60 minutes.'}

$cli=Join-Path ([IO.Path]::GetFullPath($PackageRoot)) 'archive-cli.exe'
if(!(Test-Path -LiteralPath $cli -PathType Leaf)){throw "archive-cli.exe not found: $cli"}
$root=[IO.Path]::GetFullPath($ArchiveRoot)
$parent=Split-Path -Parent $root
New-Item -ItemType Directory -Path $root -Force|Out-Null
if([string]::IsNullOrWhiteSpace($OutputJson)){$OutputJson=Join-Path $parent 'archive-endurance.json'}
if($PlanPath){$env:ARCHIVE_TEST_PLAN=[IO.Path]::GetFullPath($PlanPath)}
$basePackage=[IO.Path]::GetFullPath($(if($BasePackageRoot){$BasePackageRoot}else{$PackageRoot}))

function Assert-PackageSeal([string]$path){
    $identityPath=Join-Path $path 'build-identity.json'
    if(!(Test-Path -LiteralPath $identityPath -PathType Leaf)){throw "Missing build identity: $identityPath"}
    $identity=Get-Content -LiteralPath $identityPath -Raw|ConvertFrom-Json
    if($ExpectedCommit -and $identity.commit -ne $ExpectedCommit){throw "Unexpected package commit: $($identity.commit)"}
    if($ExpectedCiRun -and [string]$identity.run_id -ne [string]$ExpectedCiRun){throw "Unexpected package CI run: $($identity.run_id)"}
    $manifest=Join-Path $path 'SHA256SUMS.txt';if(!(Test-Path -LiteralPath $manifest -PathType Leaf)){throw "Missing package manifest: $manifest"}
    $entries=@();foreach($line in Get-Content -LiteralPath $manifest){if($line -match '^([0-9a-fA-F]{64})  (.+)$'){$entries += [pscustomobject]@{Expected=$Matches[1].ToLowerInvariant();Path=$Matches[2]}}}
    if(!$entries.Count){throw 'Package manifest is empty'}
    $listed=@{};$mismatches=@();$missing=@();foreach($entry in $entries){$listed[$entry.Path]=$true;$file=Join-Path $path ($entry.Path.Replace('/','\'));if(!(Test-Path -LiteralPath $file -PathType Leaf)){$missing+=$entry.Path}elseif((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Expected){$mismatches+=$entry.Path}}
    $unsealed=@();foreach($file in Get-ChildItem -LiteralPath $path -Recurse -File -Force){$relative=$file.FullName.Substring($path.Length+1).Replace('\','/');if($relative -ne 'SHA256SUMS.txt' -and !$listed.ContainsKey($relative)){$unsealed+=$relative}}
    if(($missing.Count -gt 0) -or ($mismatches.Count -gt 0) -or ($unsealed.Count -gt 0)){throw "Base package seal invalid: missing=$($missing.Count) mismatches=$($mismatches.Count) unsealed=$($unsealed.Count)"}
    return [ordered]@{root=$path;commit=$identity.commit;ciRunId=[string]$identity.run_id;manifestEntries=$entries.Count;manifestSha256=(Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant();unsealed=$unsealed}
}
$packageSeal=Assert-PackageSeal $basePackage
$packageFfmpeg=Join-Path $PackageRoot '3rdParty\ffmpeg\bin'
if(Test-Path -LiteralPath $packageFfmpeg -PathType Container){$env:PATH=$packageFfmpeg+';'+$env:PATH}

function Invoke-Archive([string[]]$Arguments){
    $output=& $cli @Arguments 2>&1
    [pscustomobject]@{Args=$Arguments;ExitCode=$LASTEXITCODE;Output=($output -join "`n");At=(Get-Date).ToString('o')}
}
function Get-MediaHashes{
    $hashes=[ordered]@{}
    foreach($directory in @('Video','Audio')){
        $path=Join-Path $root $directory
        if(Test-Path -LiteralPath $path){
            foreach($file in Get-ChildItem -LiteralPath $path -File -Recurse){
                $relative=$file.FullName.Substring($root.Length).TrimStart('\','/')
                $hashes[$relative]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        }
    }
    return $hashes
}

$start=Get-Date
$preflight=$null
if(!$SkipPreflight){
    $preflight=Invoke-Archive @('preflight',$root)
    if($preflight.ExitCode -ne 0){throw $preflight.Output}
}
$deadline=(Get-Date).AddMinutes($DurationMinutes)
$iterations=0;$failures=@();$baseline=$null;$maxMediaCount=0;$mediaChanges=@()
while((Get-Date)-lt $deadline){
    $iterations++
    foreach($arguments in @(
        @('scan',$root,$PlaylistUrl,'Endurance qualification'),
        @('sync-item',$root,$VideoUrl),
        @('verify-item',$root,$VideoUrl)
    )){
        $result=Invoke-Archive $arguments
        if($result.ExitCode -ne 0){$failures+=,$result;break}
    }
    if($failures.Count -gt 0){break}
    $hashes=Get-MediaHashes
    $maxMediaCount=[Math]::Max($maxMediaCount,$hashes.Count)
    if($null -eq $baseline){$baseline=$hashes}else{
        foreach($key in $baseline.Keys){
            if(!$hashes.Contains($key) -or $hashes[$key] -ne $baseline[$key]){$mediaChanges+=$key}
        }
        foreach($key in $hashes.Keys){if(!$baseline.Contains($key)){$mediaChanges+=$key}}
        if($mediaChanges.Count -gt 0){$failures+=,[pscustomobject]@{Args=@('hash-stability');ExitCode=1;Output=('Media set/hash changed: '+($mediaChanges -join ', '));At=(Get-Date).ToString('o')}}
    }
    if($failures.Count -gt 0){break}
    if(($iterations%50)-eq 0){Write-Host ("Endurance iteration {0}; remaining {1:n0} seconds" -f $iterations,[Math]::Max(0,($deadline-(Get-Date)).TotalSeconds))}
}

$tempEntries=if(Test-Path -LiteralPath (Join-Path $root 'Temp')){@(Get-ChildItem -LiteralPath (Join-Path $root 'Temp') -Force -Recurse).Count}else{0}
$journal=Test-Path -LiteralPath (Join-Path $root 'State\ArchiveMode\transaction.json')
$report=[ordered]@{
    schema_version=1
    started=$start.ToString('o')
    ended=(Get-Date).ToString('o')
    requested_minutes=$DurationMinutes
    iterations=$iterations
    failures=$failures
    preflightSkipped=[bool]$SkipPreflight
    preflightReason=if($SkipPreflight){'deliberate fake-provider overlay; exact sealed base package was manifest-validated and preflighted separately'}else{$null}
    packageSeal=$packageSeal
    basePackageRoot=$basePackage
    packageRoot=[IO.Path]::GetFullPath($PackageRoot)
    media_hashes=$baseline
    media_changes=$mediaChanges
    max_media_count=$maxMediaCount
    temp_entries=$tempEntries
    transaction_journal_present=$journal
    pass=($failures.Count -eq 0 -and (Get-Date)-ge $deadline -and !$journal -and $packageSeal.manifestEntries -gt 0)
}
$report|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $OutputJson -Encoding UTF8
if(!$report.pass){throw "Endurance failed or ended early: $OutputJson"}
Write-Host "Endurance PASS: $OutputJson"
