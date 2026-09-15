#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ScriptPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$source = [IO.File]::ReadAllText([IO.Path]::GetFullPath($ScriptPath))
$tokens = $null
$errors = $null
[System.Management.Automation.Language.Parser]::ParseInput($source, [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count -ne 0) { throw ('Harness script parse failed: ' + ($errors | Out-String)) }

$match = [regex]::Match($source, '(?s)function\s+Is-CloudPlaceholderTag\s*\([^)]*\)\s*\{.*?\n\}')
if (-not $match.Success) { throw 'Cloud Files tag classifier is missing' }
Invoke-Expression $match.Value

function Assert-Tag([uint32]$tag, [bool]$expected, [string]$name) {
    $actual = [bool](Is-CloudPlaceholderTag $tag)
    if ($actual -ne $expected) { throw "Unexpected Cloud Files classification for ${name}: $actual" }
}

$baseCloudTag = [Convert]::ToUInt32('9000001A', 16)
$oneDriveCloudTag = [Convert]::ToUInt32('9000601A', 16)
$upperCloudTag = [Convert]::ToUInt32('9000F01A', 16)
$symlinkTag = [Convert]::ToUInt32('A000000C', 16)
$junctionTag = [Convert]::ToUInt32('A0000003', 16)
Assert-Tag $baseCloudTag $true 'base Cloud tag'
Assert-Tag $oneDriveCloudTag $true 'OneDrive Cloud Files placeholder tag'
Assert-Tag $upperCloudTag $true 'upper Cloud Files family tag'
Assert-Tag $symlinkTag $false 'symbolic link tag'
Assert-Tag $junctionTag $false 'junction tag'
Assert-Tag ([uint32]0x12345678) $false 'unknown reparse tag'

if ($source -notmatch 'Get-ReparseTag\s+\$cursor') { throw 'Safe-Child does not inspect reparse tags' }
if ($source -notmatch '\$null\s+-eq\s+\$tag.*Is-CloudPlaceholderTag') { throw 'Safe-Child does not fail closed for unknown tags' }
Write-Output 'archive-local-harness-reparse-tests PASS'
