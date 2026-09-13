# strings
$year_end = "2026"
$month_end = "9"
$version = "2"

$version = "$year_end.$month_end.$version"

# alternative strings
$version_number = $version.Replace(".", "")
$version_posix = $version -replace "\.\d+$", ("-" + ($version.SubString($version.LastIndexOf(".") + 1)))
$version_posix_long = $version_posix + "-1"
$version_windows = $version.Replace(".", ",")

# Set-Content joins lines with CRLF on Windows PowerShell, but .gitattributes
# declares these files LF ("* text=auto eol=lf"). Every deploy therefore left
# eight tracked files modified with an empty content diff, which blocked the
# next pull until someone discarded them by hand. Rewrite them LF.
#
# This works on the raw bytes so the encoding is untouched: Get-Content and
# Set-Content round-trip the ANSI codepage, which is what preserves the UTF-8
# em dashes in readme.json, and re-encoding here would corrupt them.
#
# code_doc64.cmd is deliberately not in the list below: *.cmd is declared
# eol=crlf because cmd.exe needs CRLF, so its CRLF is correct.
function ConvertTo-LfEol {
  param([string] $Path)
  # .NET resolves a relative path against the process directory, which
  # Set-Location does not change, so resolve it through PowerShell first.
  $full = (Resolve-Path -LiteralPath $Path).ProviderPath
  $latin1 = [System.Text.Encoding]::GetEncoding(28591)
  $text = $latin1.GetString([System.IO.File]::ReadAllBytes($full))
  [System.IO.File]::WriteAllBytes($full, $latin1.GetBytes(($text -replace "`r`n", "`n")))
}

# update source version header
(Get-Content ..\shared\version.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@VERSION_NUMBER@", $version_number } | Set-Content ..\shared\version.h
(Get-Content code_doc64.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@VERSION_WINDOWS@", $version_windows } | Set-Content code_doc64.cmd


(Get-Content ..\..\\programs\deploy\util\readme\readme.json.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@YEAR@", $year_end } | Set-Content ..\..\\programs\deploy\util\readme\readme.json
# The VS Code extension manifest. release-build.yml stamps the packaged .vsix
# from the tag, so the SHIPPED version was always right; the committed file
# sat two releases behind because nothing here touched it.
$package_json = "..\..\tools\lsp\clients\vscode\package.json"
(Get-Content $package_json) | ForEach-Object { $_ -replace '("version":\s*")[0-9.]+(")', ('${1}' + $version + '${2}') } | Set-Content $package_json


# update window resource files
(Get-Content ..\compiler\vs\objeck.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@YEAR_END@", $year_end } | ForEach-Object { $_ -replace "@VERSION_WINDOWS@", $version_windows } | Set-Content ..\compiler\vs\objeck.rc
(Get-Content ..\vm\vs\objeck.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@YEAR_END@", $year_end } | ForEach-Object { $_ -replace "@VERSION_WINDOWS@", $version_windows } | Set-Content ..\vm\vs\objeck.rc
(Get-Content ..\debugger\vs\objeck.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@YEAR_END@", $year_end } | ForEach-Object { $_ -replace "@VERSION_WINDOWS@", $version_windows } | Set-Content ..\debugger\vs\objeck.rc
(Get-Content ..\repl\vs\objeck.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@YEAR_END@", $year_end } | ForEach-Object { $_ -replace "@VERSION_WINDOWS@", $version_windows } | Set-Content ..\repl\vs\objeck.rc
(Get-Content ..\utils\launcher\vs\builder\objeck.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ForEach-Object { $_ -replace "@YEAR_END@", $year_end } | ForEach-Object { $_ -replace "@VERSION_WINDOWS@", $version_windows } | Set-Content ..\utils\launcher\vs\builder\objeck.rc


# every file stamped above is declared LF; see ConvertTo-LfEol
@(
  "..\shared\version.h",
  "..\..\programs\deploy\util\readme\readme.json",
  $package_json,
  "..\compiler\vs\objeck.rc",
  "..\vm\vs\objeck.rc",
  "..\debugger\vs\objeck.rc",
  "..\repl\vs\objeck.rc",
  "..\utils\launcher\vs\builder\objeck.rc"
) | ForEach-Object { ConvertTo-LfEol $_ }
