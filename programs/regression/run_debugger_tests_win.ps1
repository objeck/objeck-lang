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

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Debugger
    $psi.Arguments = "-b `"$TestBin`" -src `"$SrcDir`""
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true

    $proc = [System.Diagnostics.Process]::Start($psi)

    # Send commands with delays for interactive processing
    foreach ($cmd in $Commands) {
        Start-Sleep -Milliseconds 500
        $proc.StandardInput.WriteLine($cmd)
    }

    # Always quit at the end
    Start-Sleep -Milliseconds 500
    $proc.StandardInput.WriteLine("q")
    $proc.StandardInput.Close()

    # Read output with timeout
    $output = ""
    $task = $proc.StandardOutput.ReadToEndAsync()
    if ($task.Wait(30000)) {
        $output = $task.Result
    }

    if (-not $proc.HasExited) {
        $proc.Kill()
    }
    $proc.WaitForExit()

    # Save output
    $output | Out-File -FilePath "$ResultsDir\debugger_${TestName}.log" -Encoding utf8

    # Check expected patterns (use literal string match to avoid wildcard issues with [] etc.)
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

Write-Host ""
Write-Host "========================================"
Write-Host "  Results: $PassCount passed, $FailCount failed"
Write-Host "========================================"

if ($FailCount -gt 0) { exit 1 } else { exit 0 }
