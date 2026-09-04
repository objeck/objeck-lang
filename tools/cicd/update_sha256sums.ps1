# Regenerate a release's SHA256SUMS from the PUBLISHED assets.
#
# Signing rewrites the MSIs, so a manifest generated before signing describes
# bytes nobody can download. v2026.8.3 shipped exactly that: every user running
# `sha256sum -c SHA256SUMS` would have seen both Windows installers FAIL, on the
# two files whose integrity matters most. sign_release.cmd re-uploads the signed
# MSIs but has never touched the manifest, so this ran by hand or not at all.
#
# Hashes the assets as downloaded from the release -- not local staging copies --
# because the manifest describes what users get, and "the file I just signed" and
# "the file the release serves" are only the same thing if the upload worked.
#
# Usage: update_sha256sums.ps1 <version>          e.g. 2026.9.0

param([Parameter(Mandatory = $true)][string]$Version)

$ErrorActionPreference = "Stop"
$tag = "v$Version"
$gh = "C:\Program Files\GitHub CLI\gh.exe"
if (-not (Test-Path $gh)) { $gh = "gh" }

$work = Join-Path $env:TEMP "sha256sums-$Version"
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Path $work | Out-Null

Write-Output "Downloading published assets for $tag ..."
& $gh release download $tag --dir $work
if ($LASTEXITCODE -ne 0) { throw "failed to download assets for $tag" }

$manifest = Join-Path $work "SHA256SUMS"
if (-not (Test-Path $manifest)) { throw "no SHA256SUMS asset on $tag" }

# Rebuild every line from the bytes on disk, preserving the manifest's order and
# file list. Recomputing all of them (not just the MSIs) means the result is a
# statement about the release as it stands, not a patch applied on trust.
$lines = Get-Content $manifest | Where-Object { $_.Trim() -ne "" }
$out = @()
$changed = @()
foreach ($line in $lines) {
    $parts = $line -split '\s+', 2
    $oldHash = $parts[0]
    $name = $parts[1].Trim().TrimStart('*')
    $path = Join-Path $work $name
    if (-not (Test-Path $path)) { throw "manifest names '$name' but it is not an asset of $tag" }
    $newHash = (Get-FileHash $path -Algorithm SHA256).Hash.ToLower()
    if ($newHash -ne $oldHash.ToLower()) { $changed += $name }
    $out += "$newHash  $name"
}

if ($changed.Count -eq 0) {
    Write-Output "SHA256SUMS already matches all $($out.Count) published assets - nothing to do."
    exit 0
}

Write-Output ""
Write-Output "Stale entries ($($changed.Count) of $($out.Count)):"
$changed | ForEach-Object { Write-Output "  $_" }

# Write LF-terminated: sha256sum(1) on Linux/macOS is the consumer, and CRLF
# makes it report every line as improperly formatted.
$text = ($out -join "`n") + "`n"
[System.IO.File]::WriteAllText($manifest, $text, (New-Object System.Text.UTF8Encoding $false))

Write-Output ""
Write-Output "Uploading corrected SHA256SUMS ..."
& $gh release upload $tag $manifest --clobber
if ($LASTEXITCODE -ne 0) { throw "failed to upload SHA256SUMS" }

# Prove it against the release, not against what we just wrote locally.
$verify = Join-Path $env:TEMP "sha256sums-verify-$Version"
if (Test-Path $verify) { Remove-Item $verify -Recurse -Force }
New-Item -ItemType Directory -Path $verify | Out-Null
& $gh release download $tag --pattern "SHA256SUMS" --dir $verify | Out-Null
$published = Get-Content (Join-Path $verify "SHA256SUMS") | Where-Object { $_.Trim() -ne "" }

$bad = @()
foreach ($line in $published) {
    $parts = $line -split '\s+', 2
    $name = $parts[1].Trim().TrimStart('*')
    $actual = (Get-FileHash (Join-Path $work $name) -Algorithm SHA256).Hash.ToLower()
    if ($actual -ne $parts[0].ToLower()) { $bad += $name }
}

Remove-Item $work -Recurse -Force
Remove-Item $verify -Recurse -Force

if ($bad.Count -gt 0) {
    Write-Output ""
    Write-Output "VERIFICATION FAILED for: $($bad -join ', ')"
    exit 1
}

Write-Output ""
Write-Output "SHA256SUMS verified against all $($published.Count) published assets."
