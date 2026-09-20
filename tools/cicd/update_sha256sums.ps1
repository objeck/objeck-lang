# Regenerate a release's SHA256SUMS from the PUBLISHED assets.
#
# Signing rewrites the MSIs, so a manifest generated before signing describes
# bytes nobody can download. v2026.8.3 shipped exactly that: every user running
# `sha256sum -c SHA256SUMS` would have seen both Windows installers FAIL, on the
# two files whose integrity matters most. sign_release.cmd re-uploads the signed
# MSIs but has never touched the manifest, so this ran by hand or not at all.
#
# The hashes describe what the release SERVES, never a local staging copy --
# "the file I just signed" and "the file the release serves" are only the same
# thing if the upload worked. GitHub computes a SHA-256 of every asset it stores
# and returns it as `digest: sha256:...` on the release API, so that property is
# satisfied without moving the bytes: the digest is a statement about the stored
# object, made by the host that serves it.
#
# This used to download every asset instead -- `gh release download` with no
# pattern, ~300 MB for a full release. On v2026.9.6 the release CDN was
# throttling the signing machine to ~40 KB/s (a Cloudflare control on the same
# link measured 19.9 MB/s), which turned this step into a 2+ hour job while the
# published manifest already described the pre-signing MSIs. A release-critical
# repair that takes hours on a slow link does not get run.
#
# Assets whose digest the API does not return -- an older release, or a future
# API change -- are still downloaded and hashed, so the manifest is always
# complete rather than partially trusted.
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

# --- what the release holds, and what GitHub says each asset hashes to --------
Write-Output "Reading published assets for $tag ..."
$json = & $gh api "repos/{owner}/{repo}/releases/tags/$tag"
if ($LASTEXITCODE -ne 0) { throw "failed to read release $tag" }
$assets = (ConvertFrom-Json ($json -join "`n")).assets

$digest = @{}
$noDigest = @()
foreach ($a in $assets) {
    if ($a.digest -and $a.digest.StartsWith("sha256:")) {
        $digest[$a.name] = $a.digest.Substring(7).ToLower()
    }
    else {
        $noDigest += $a.name
    }
}
Write-Output "  $($assets.Count) assets, $($digest.Count) with a published digest"

# The manifest itself is small and is the one asset we must read, not hash.
& $gh release download $tag --pattern "SHA256SUMS" --dir $work
if ($LASTEXITCODE -ne 0) { throw "failed to download SHA256SUMS for $tag" }
$manifest = Join-Path $work "SHA256SUMS"
if (-not (Test-Path $manifest)) { throw "no SHA256SUMS asset on $tag" }

# Only the assets GitHub did not give us a digest for have to travel.
if ($noDigest.Count -gt 0) {
    Write-Output "  no digest for $($noDigest.Count): $($noDigest -join ', ') -- downloading those"
    foreach ($name in $noDigest) {
        & $gh release download $tag --pattern $name --dir $work --clobber
        if ($LASTEXITCODE -ne 0) { throw "failed to download $name" }
        $digest[$name] = (Get-FileHash (Join-Path $work $name) -Algorithm SHA256).Hash.ToLower()
    }
}

# --- rebuild every line, preserving the manifest's order and file list --------
# Recomputing all of them (not just the MSIs) means the result is a statement
# about the release as it stands, not a patch applied on trust.
$lines = Get-Content $manifest | Where-Object { $_.Trim() -ne "" }
$out = @()
$changed = @()
foreach ($line in $lines) {
    $parts = $line -split '\s+', 2
    $oldHash = $parts[0]
    $name = $parts[1].Trim().TrimStart('*')
    if (-not $digest.ContainsKey($name)) { throw "manifest names '$name' but it is not an asset of $tag" }
    $newHash = $digest[$name]
    if ($newHash -ne $oldHash.ToLower()) { $changed += $name }
    $out += "$newHash  $name"
}

if ($changed.Count -eq 0) {
    Write-Output "SHA256SUMS already matches all $($out.Count) published assets - nothing to do."
    Remove-Item $work -Recurse -Force
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

# --- prove it against the release, not against what we just wrote locally -----
# Re-read both sides: the manifest as published, and the digests as published
# AFTER the upload, so a clobber that silently kept the old asset is caught.
$verify = Join-Path $env:TEMP "sha256sums-verify-$Version"
if (Test-Path $verify) { Remove-Item $verify -Recurse -Force }
New-Item -ItemType Directory -Path $verify | Out-Null
& $gh release download $tag --pattern "SHA256SUMS" --dir $verify | Out-Null
$published = Get-Content (Join-Path $verify "SHA256SUMS") | Where-Object { $_.Trim() -ne "" }

$json2 = & $gh api "repos/{owner}/{repo}/releases/tags/$tag"
if ($LASTEXITCODE -ne 0) { throw "failed to re-read release $tag" }
$after = @{}
foreach ($a in (ConvertFrom-Json ($json2 -join "`n")).assets) {
    if ($a.digest -and $a.digest.StartsWith("sha256:")) { $after[$a.name] = $a.digest.Substring(7).ToLower() }
    elseif ($digest.ContainsKey($a.name)) { $after[$a.name] = $digest[$a.name] }
}

$bad = @()
foreach ($line in $published) {
    $parts = $line -split '\s+', 2
    $name = $parts[1].Trim().TrimStart('*')
    if (-not $after.ContainsKey($name)) { $bad += "$name (no published digest)"; continue }
    if ($after[$name] -ne $parts[0].ToLower()) { $bad += $name }
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
