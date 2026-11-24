# Render a Windows resource template (objeck.in) into its .rc, stamping the
# version from core/shared/version.h.
#
# The five objeck.rc files used to be committed: update_version.ps1 generated
# them and every release re-committed them, which put a version-stamped copy of
# the same template in five places and made "a plain MSBuild run" depend on
# having run the release script first. They are now gitignored build outputs,
# produced by the GenerateVersionedRc target in rc_version.targets, which every
# project that compiles one imports. That target calls this script, so a plain
# `MSBuild objeck.sln`, a Visual Studio build, deploy_windows.cmd, CI and the
# Coverity scan all get the same file.
#
# Substitutions (the same three update_version.ps1 always made):
#   @VERSION@          2026.11.3      VERSION_STRING in version.h
#   @YEAR_END@         2026           its first component
#   @VERSION_WINDOWS@  2026,11,3      the FILEVERSION/PRODUCTVERSION form
#
# Works on raw bytes (latin-1 round-trip) so the template's encoding is
# untouched, writes LF to match .gitattributes, and leaves the output alone when
# it is already identical so a no-op build does not force a resource recompile
# and relink.
param(
  [Parameter(Mandatory = $true)] [string] $Template,
  [Parameter(Mandatory = $true)] [string] $Output,
  [string] $VersionHeader
)

$ErrorActionPreference = "Stop"

# $PSScriptRoot is empty inside a param() default on Windows PowerShell 5.1.
if (-not $VersionHeader) {
  $VersionHeader = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "version.h"
}

$header = [System.IO.File]::ReadAllText($VersionHeader)
$m = [regex]::Match($header, '#define\s+VERSION_STRING\s+L"(\d+)\.(\d+)\.(\d+)"')
if (-not $m.Success) {
  throw "gen_rc.ps1: no VERSION_STRING of the form L`"Y.M.R`" in $VersionHeader"
}
$year = $m.Groups[1].Value
$version = "$year.$($m.Groups[2].Value).$($m.Groups[3].Value)"
$version_windows = $version.Replace(".", ",")

$latin1 = [System.Text.Encoding]::GetEncoding(28591)
$text = $latin1.GetString([System.IO.File]::ReadAllBytes($Template))
$text = $text.Replace("@VERSION_WINDOWS@", $version_windows).Replace("@VERSION@", $version).Replace("@YEAR_END@", $year)
$text = $text -replace "`r`n", "`n"
# Set-Content, which update_version.ps1 used, always ended the file with a newline.
if (-not $text.EndsWith("`n")) { $text += "`n" }

$left = [regex]::Match($text, '@[A-Z][A-Z_]*@')
if ($left.Success) {
  throw "gen_rc.ps1: $Template still has the placeholder $($left.Value) after substitution"
}

$bytes = $latin1.GetBytes($text)
if ((Test-Path -LiteralPath $Output) -and
    [System.Linq.Enumerable]::SequenceEqual([byte[]][System.IO.File]::ReadAllBytes($Output), $bytes)) {
  exit 0
}
[System.IO.File]::WriteAllBytes($Output, $bytes)
Write-Host "gen_rc.ps1: wrote $Output (version $version)"
