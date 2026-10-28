# Prints the release version and the MSI ProductVersion it maps to, as
#   <release> <msi>
# e.g. "2026.10.2 26.10.2". deploy_windows.cmd reads both with for /f.
#
# Why a mapping: an MSI ProductVersion is major.minor.build with major <= 255,
# minor <= 255 and build <= 65535, and Windows Installer compares only those
# three fields. A release is <year>.<month>.<release>, and 2026 does not fit in
# a major field. So the year drops its century:
#
#     2026.10.2  ->  26.10.2
#     2026.11.0  ->  26.11.0
#     2027.1.0   ->  27.1.0
#
# This keeps every release greater than the one before it, which is what
# MajorUpgrade needs to replace an installed release, and it holds until 2255.
# Every MSI before v2026.10.2 was built as 0.0.0, so the first versioned MSI
# also upgrades every earlier one.
#
# Only the numeric ProductVersion is mapped. The full release string is what
# people see: it is in the file name, the product name in Add/Remove Programs,
# and HKLM\Software\Objeck\Version.
param(
	[string] $VersionHeader = (Join-Path $PSScriptRoot "..\shared\version.h")
)

$m = Select-String -LiteralPath $VersionHeader -Pattern 'VERSION_STRING\s+L"((\d+)\.(\d+)\.(\d+)[^"]*)"' | Select-Object -First 1
if(-not $m) {
	[Console]::Error.WriteLine("msi_version.ps1: no VERSION_STRING in $VersionHeader")
	exit 1
}

$g = $m.Matches[0].Groups
$release = $g[1].Value
$year = [int]$g[2].Value
$month = [int]$g[3].Value
$build = [int]$g[4].Value

if($year -lt 2000 -or $year -gt 2255 -or $month -gt 255 -or $build -gt 65535) {
	[Console]::Error.WriteLine("msi_version.ps1: $release does not map to an MSI ProductVersion")
	exit 1
}

"{0} {1}.{2}.{3}" -f $release, ($year - 2000), $month, $build
