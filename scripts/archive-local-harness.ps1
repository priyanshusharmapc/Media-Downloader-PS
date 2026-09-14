param(
    [Parameter(Mandatory=$true)][string]$ArchiveRoot,
    [Parameter(Mandatory=$true)][string]$PlaylistUrl,
    [Parameter(Mandatory=$true)][string]$VideoUrl
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$cli = Join-Path $here 'archive-cli.exe'
if (!(Test-Path $cli)) { throw "archive-cli.exe not found beside this harness script: $cli" }

Write-Host '=== Archive Mode portable preflight ==='
& $cli preflight $ArchiveRoot
if ($LASTEXITCODE -ne 0) { throw "Archive preflight failed with exit $LASTEXITCODE" }

Write-Host '=== Real playlist discovery and reconciliation ==='
$scan = & $cli scan $ArchiveRoot $PlaylistUrl 'Local harness playlist' 2>&1
$scanExit = $LASTEXITCODE
$scan | ForEach-Object { Write-Host $_ }
if ($scanExit -ne 0) { throw "Playlist scan was not complete. archive-cli exit=$scanExit" }
$observedLine = $scan | Where-Object { $_ -match '^observed=([0-9]+)$' } | Select-Object -First 1
if (!$observedLine) { throw 'Playlist scan did not report observed=<count>' }
$observed = [int](($observedLine -split '=')[1])
if ($observed -lt 1) { throw 'Playlist scan completed but observed zero items' }

Write-Host '=== Real video and audio Archive execution ==='
& $cli sync-item $ArchiveRoot $VideoUrl
if ($LASTEXITCODE -ne 0) { throw "Archive media sync failed with exit $LASTEXITCODE" }

$video = Get-ChildItem (Join-Path $ArchiveRoot 'Video') -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
$audio = Get-ChildItem (Join-Path $ArchiveRoot 'Audio') -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if (!$video -or !$audio) { throw 'Archive execution did not produce both video and audio representations' }

Write-Host '=== Local Archive harness qualification PASS ==='
Write-Host "Observed playlist items: $observed"
Write-Host "Video: $($video.FullName)"
Write-Host "Audio: $($audio.FullName)"
