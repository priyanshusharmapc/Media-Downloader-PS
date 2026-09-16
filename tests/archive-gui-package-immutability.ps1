#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackageRoot,
    [Parameter(Mandatory=$true)][string]$ArchiveRoot,
    [Parameter(Mandatory=$true)][string]$OutputJson
)

$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$package=[IO.Path]::GetFullPath($PackageRoot)
$archive=[IO.Path]::GetFullPath($ArchiveRoot)
$manifest=Join-Path $package 'SHA256SUMS.txt'
$entries=@();foreach($line in Get-Content -LiteralPath $manifest){if($line -match '^([0-9a-fA-F]{64})  (.+)$'){$entries += [pscustomobject]@{Expected=$Matches[1].ToLowerInvariant();Path=$Matches[2]}}}
$gui=Join-Path $package 'media-downloader.exe'
New-Item -ItemType Directory -Path $archive -Force|Out-Null
$env:ARCHIVE_GUI_TEST_HOOK='1'
$env:ARCHIVE_GUI_TEST_ROOT=$archive
$env:QT_QPA_PLATFORM='offscreen'
$process=Start-Process -FilePath $gui -WorkingDirectory $package -ArgumentList @('-platform','offscreen','--archive-settings-smoke',$archive) -PassThru
$configRoot=$env:ARCHIVE_TEST_CONFIG_ROOT
$settingsPath=if($configRoot){Join-Path $configRoot 'archive-mode.ini'}else{''}
$settingsDeadline=(Get-Date).AddSeconds(90)
while((Get-Date)-lt $settingsDeadline -and !(Test-Path -LiteralPath $settingsPath -PathType Leaf)){
    Start-Sleep -Milliseconds 250
    $process.Refresh()
    if($process.HasExited){break}
}
$settingsExists=Test-Path -LiteralPath $settingsPath -PathType Leaf
if(!$settingsExists){if(!$process.HasExited){$process.Kill()};throw 'GUI settings smoke did not persist the user setting'}
if(!$process.HasExited){$process.Kill();$process.WaitForExit(10000)|Out-Null}
$missing=@();$mismatch=@();$listed=@{};foreach($entry in $entries){$listed[$entry.Path]=$true;$path=Join-Path $package ($entry.Path.Replace('/','\'));if(!(Test-Path -LiteralPath $path -PathType Leaf)){$missing+=$entry.Path}elseif((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Expected){$mismatch+=$entry.Path}}
$unsealed=@();foreach($file in Get-ChildItem -LiteralPath $package -Recurse -File -Force){$relative=$file.FullName.Substring($package.Length+1).Replace('\','/');if($relative -ne 'SHA256SUMS.txt' -and !$listed.ContainsKey($relative)){$unsealed+=$relative}}
$report=[ordered]@{observedAt=(Get-Date -Format o);packageRoot=$package;archiveRoot=$archive;exitCode=if($process.HasExited){$process.ExitCode}else{$null};settingsExists=$settingsExists;missing=$missing;mismatches=$mismatch;unsealed=$unsealed;pass=($settingsExists -and $missing.Count -eq 0 -and $mismatch.Count -eq 0 -and $unsealed.Count -eq 0)}
$report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $OutputJson -Encoding UTF8
if(!$report.pass){throw "GUI package immutability failed: $OutputJson"}
Write-Host "GUI package immutability PASS: $OutputJson"
