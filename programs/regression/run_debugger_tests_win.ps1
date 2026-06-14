param(
    [string]$Debugger,
    [string]$TestBin,
    [string]$SrcDir,
    [string]$ResultsDir
)

$PassCount = 0
$FailCount = 0

function Run-DebuggerTest {
    param(
        [string]$TestName,
        [string[]]$Commands,
        [string[]]$ExpectedPatterns
    )

    Write-Host -NoNewline "Running: ${TestName}..."

    $inputFile  = "$env:TEMP\obd_input_$TestName.txt"
    $outputFile = "$env:TEMP\obd_output_$TestName.txt"
    $errFile    = "$env:TEMP\obd_err_$TestName.txt"
    $batchFile  = "$env:TEMP\obd_run_$TestName.bat"

    # Write commands file (CRLF, ascii)
    ($Commands + @("q")) | Out-File -FilePath $inputFile -Encoding ascii

    # Build a temp batch file that uses native cmd I/O redirection.
    # This avoids the Start-Process stdout-pipe race condition on CI runners.
    $batch = "@echo off`r`n"
    $batch += "`"$Debugger`" -b `"$TestBin`" -src `"$SrcDir`" < `"$inputFile`" > `"$outputFile`" 2> `"$errFile`"`r`n"
    [System.IO.File]::WriteAllText($batchFile, $batch, [System.Text.Encoding]::ASCII)

    # cmd /c runs the batch synchronously; output files are fully written on return
    $exitCode = 0
    try {
        & cmd.exe /c "`"$batchFile`""
        $exitCode = $LASTEXITCODE
    }
    catch {
        $exitCode = -1
    }

    $output = ""
    if (Test-Path $outputFile) {
        $output = Get-Content $outputFile -Raw -ErrorAction SilentlyContinue
        if ($null -eq $output) { $output = "" }
    }

    $errOutput = ""
    if (Test-Path $errFile) {
        $errOutput = Get-Content $errFile -Raw -ErrorAction SilentlyContinue
        if ($null -eq $errOutput) { $errOutput = "" }
    }

    # Clean up temp files
    Remove-Item $inputFile  -Force -ErrorAction SilentlyContinue
    Remove-Item $outputFile -Force -ErrorAction SilentlyContinue
    Remove-Item $errFile    -Force -ErrorAction SilentlyContinue
    Remove-Item $batchFile  -Force -ErrorAction SilentlyContinue

    # Save full output to results log
    $output | Out-File -FilePath "$ResultsDir\debugger_${TestName}.log" -Encoding utf8

    # Check expected patterns
    $allPass = $true
    foreach ($pattern in $ExpectedPatterns) {
        if (-not $output.Contains($pattern)) {
            $allPass = $false
            Write-Host ""
            Write-Host "  Missing expected output: '$pattern'"
        }
    }

    if ($allPass) {
        Write-Host " [PASS]"
        $script:PassCount++
    } else {
        if ($errOutput.Trim().Length -gt 0) {
            Write-Host "  stderr: $errOutput"
        }
        Write-Host "  [FAIL]"
        $script:FailCount++
    }
}

# Test 1: Help command
Run-DebuggerTest "help" @("h") @("Commands:", "b, break", "s, step", "n, next", "p, print", "q, quit")

# Test 2: Breakpoint set/list/delete
Run-DebuggerTest "breakpoints" @(
    "b debugger_test.obs:30",
    "b debugger_test.obs:34",
    "breaks",
    "d debugger_test.obs:30",
    "breaks"
) @("added breakpoint: file='debugger_test.obs:30'", "added breakpoint: file='debugger_test.obs:34'", "removed breakpoint: file='debugger_test.obs:30'")

# Test 3: Run and hit breakpoint
Run-DebuggerTest "run_break" @(
    "b debugger_test.obs:34",
    "r",
    "c"
) @("added breakpoint", "break: file=", "method='Main->Main(..)'")

# Test 4: Print variables
Run-DebuggerTest "print_vars" @(
    "b debugger_test.obs:38",
    "r",
    "p sum",
    "p values",
    "p counter",
    "c"
) @("print: type=Int/Byte/Bool, value=100", "print: type=Int[], value=", "print: type=Counter")

# Test 5: Step into method
Run-DebuggerTest "step_into" @(
    "b debugger_test.obs:34",
    "r",
    "s",
    "l",
    "c"
) @("Counter->Increment(..)")

# Test 6: Step over (next)
Run-DebuggerTest "step_over" @(
    "b debugger_test.obs:34",
    "r",
    "n",
    "n",
    "c"
) @("break: file=", "Main->Main(..)")

# Test 7: Stack trace
Run-DebuggerTest "stack_trace" @(
    "b debugger_test.obs:9",
    "r",
    "stack",
    "c"
) @("stack:")

# Test 8: List source
Run-DebuggerTest "list_source" @(
    "b debugger_test.obs:34",
    "r",
    "l",
    "c"
) @("counter->Increment()")

# Test 9: Memory command
Run-DebuggerTest "memory" @(
    "b debugger_test.obs:38",
    "r",
    "m",
    "c"
) @("memory: allocated=")

# Test 10: Info command
Run-DebuggerTest "info" @(
    "b debugger_test.obs:34",
    "r",
    "i",
    "i class=Counter",
    "c"
) @("general info:", "class: type=Counter")

# Test 11: Print @self and instance vars
Run-DebuggerTest "print_self" @(
    "b debugger_test.obs:9",
    "r",
    "p @self",
    "p @count",
    "c"
) @("print: type=Counter", "print: type=Int/Byte/Bool")

# Test 12: Step out (jump)
Run-DebuggerTest "step_out" @(
    "b debugger_test.obs:34",
    "r",
    "s",
    "j",
    "c"
) @("Counter->Increment(..)", "Main->Main(..)")

# Test 13: Full program execution
Run-DebuggerTest "full_run" @(
    "r"
) @("Sum=100, Counter=3", "Count is greater than 2", "Factorial(5)=120")

# Test 14: Clear breakpoints
Run-DebuggerTest "clear_breaks" @(
    "b debugger_test.obs:30",
    "b debugger_test.obs:34",
    "clear",
    "y",
    "breaks"
) @("no breakpoints defined.")

# Test 15: Conditional breakpoint (b file:line if <expr>)
# Factorial(5) recurses n = 5,4,3,2,1; the condition makes line 51
# fire only when n = 3, exercising the "if <expr>" clause.
Run-DebuggerTest "conditional_break" @(
    "b debugger_test.obs:51 if n = 3",
    "r",
    "p n",
    "c"
) @("added breakpoint", "break: file=", "Main->Factorial", "print: type=Int/Byte/Bool, value=3")

Write-Host ""
Write-Host "========================================"
Write-Host "  Results: $PassCount passed, $FailCount failed"
Write-Host "========================================"

if ($FailCount -gt 0) { exit 1 } else { exit 0 }
