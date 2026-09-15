#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackageRoot,
    [Parameter(Mandatory=$true)][string]$ArchiveRoot,
    [Parameter(Mandatory=$true)][string]$PlaylistUrl,
    [Parameter(Mandatory=$true)][string]$VideoUrl,
    [int]$DurationMinutes = 60,
    [string]$PlanPath,
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
$preflight=Invoke-Archive @('preflight',$root)
if($preflight.ExitCode -ne 0){throw $preflight.Output}
$deadline=(Get-Date).AddMinutes($DurationMinutes)
$iterations=0;$failures=@();$baseline=$null;$maxMediaCount=0
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
            if(!$hashes.Contains($key) -or $hashes[$key] -ne $baseline[$key]){
                $failures+=,[pscustomobject]@{Args=@('hash-stability');ExitCode=1;Output="Media hash changed: $key";At=(Get-Date).ToString('o')};break
            }
        }
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
    media_hashes=$baseline
    max_media_count=$maxMediaCount
    temp_entries=$tempEntries
    transaction_journal_present=$journal
    pass=($failures.Count -eq 0 -and (Get-Date)-ge $deadline -and !$journal)
}
$report|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $OutputJson -Encoding UTF8
if(!$report.pass){throw "Endurance failed or ended early: $OutputJson"}
Write-Host "Endurance PASS: $OutputJson"
