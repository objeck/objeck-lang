param(
    [string]$Debugger,
    [string]$TestBin,
    [string]$CollBin,
    [string]$EvalBin,
    [string]$ThreadBin,
    [string]$SrcDir,
    [string]$ResultsDir
)

$PassCount = 0
$FailCount = 0

function Run-DebuggerTest {
    param(
        [string]$TestName,
        [string[]]$Commands,
        [string[]]$ExpectedPatterns,
        [string]$Bin = $TestBin
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
    $batch += "`"$Debugger`" -b `"$Bin`" -src `"$SrcDir`" < `"$inputFile`" > `"$outputFile`" 2> `"$errFile`"`r`n"
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

# Test 4b: Collection sizes. `print` reached through slot 0 into the backing
# array or root node, so a Vector always reported 1, a Map reported a child
# pointer, and printing a List dereferenced its @size as a pointer and crashed.
Run-DebuggerTest "print_collections" @(
    "b debugger_coll_test.obs:35",
    "r",
    "p vec",
    "p map",
    "p hash",
    "p list",
    "c"
) @(
    "print: type=Collection.Vector, size=5",
    "print: type=Collection.Map, size=4",
    "print: type=Collection.Hash, size=3",
    "print: type=Collection.List, size=6"
) -Bin $CollBin

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

# Test 16: Frame navigation (frame / up / down / locals across frames)
# Stop in Factorial at n=3; the caller frame has n=4.
Run-DebuggerTest "frame_nav" @(
    "b debugger_test.obs:51 if n = 3",
    "r",
    "locals",
    "up",
    "locals",
    "down",
    "c"
) @("locals (frame #", "value=3", "frame #", "value=4")

# Test 17: set <var> = <value> mutates a live variable
Run-DebuggerTest "set_var" @(
    "b debugger_test.obs:51 if n = 3",
    "r",
    "set n = 99",
    "p n",
    "c"
) @("set: value=99", "print: type=Int/Byte/Bool, value=99")

# Test 18: breakpoint by method (b Class->Method) lands on the first body line
Run-DebuggerTest "method_break" @(
    "b Main->Factorial",
    "r",
    "p n",
    "c"
) @("added breakpoint", "Main->Factorial", "print: type=Int/Byte/Bool, value=5")

# Test 19: temporary (one-shot) breakpoint fires once
Run-DebuggerTest "tbreak" @(
    "tbreak debugger_test.obs:51",
    "r",
    "p n",
    "c"
) @("[temporary]", "break: file=", "print: type=Int/Byte/Bool, value=5", "Factorial(5)=120")

# Test 20: disable suppresses a breakpoint; program runs to completion
Run-DebuggerTest "disable_break" @(
    "b debugger_test.obs:51",
    "disable 1",
    "r"
) @("disabled 1 breakpoint", "Factorial(5)=120")

# Test 21: ignore count skips the next N hits (n = 5,4 skipped -> break at n=3)
Run-DebuggerTest "ignore_count" @(
    "b debugger_test.obs:51",
    "ignore 1 2",
    "r",
    "p n",
    "c"
) @("will be ignored", "print: type=Int/Byte/Bool, value=3")

# Test 22: until <line> runs to a line in the current frame
Run-DebuggerTest "until_line" @(
    "b debugger_test.obs:46",
    "r",
    "until 47",
    "p result",
    "c"
) @("running until", "break: file=", "print: type=Int/Byte/Bool, value=120")

# Test 23: breakpoint on a non-executable line is relocated with a note
Run-DebuggerTest "nonexec_line" @(
    "b debugger_test.obs:1",
    "q"
) @("has no executable code", "added breakpoint")

# Test 24: watchpoint breaks when a watched variable changes
Run-DebuggerTest "watchpoint" @(
    "b debugger_test.obs:51 if n = 3",
    "r",
    "watch n",
    "c"
) @("added watchpoint", "watch #1 changed")

# ---------------------------------------------------------------------------
# Regression tests for the expression evaluator and breakpoint bookkeeping.
#
# Each of these fails on the previous build. Four of the behaviours they pin
# down used to report SUCCESS while doing the wrong thing, which is why none of
# them was noticed: an assertion that something merely "ran" would still pass.
# ---------------------------------------------------------------------------

# Test 25: 'delete <id>' removes THAT breakpoint. It used to parse as a
# location-less delete and silently remove the one at the current line instead.
Run-DebuggerTest "delete_by_id" @(
    "b debugger_eval_test.obs:56",
    "b debugger_eval_test.obs:58",
    "delete 2",
    "breaks",
    "delete 99"
) @("break #1:", "removed breakpoint #2", "no breakpoint with id #99") $EvalBin

# Test 26: 'unwatch' with no id removes EVERY watchpoint. erase() already
# returns the next element, so the loop's ++iter skipped every second one and
# left survivors behind while reporting removal.
Run-DebuggerTest "unwatch_all" @(
    "b debugger_eval_test.obs:56",
    "r",
    "watch zero",
    "watch five",
    "watch total",
    "unwatch",
    "watches"
) @("removed 3 watchpoint(s)", "no watchpoints defined") $EvalBin

# Test 27: 'watches' names what it is watching. The CLI parser passed an empty
# string for the expression text, so the listing -- and the DAP stop message
# that shares the field -- could only print an id.
Run-DebuggerTest "watches_show_expression" @(
    "b debugger_eval_test.obs:56",
    "r",
    "watch five",
    "watches"
) @("watch #1: five") $EvalBin

# Test 28: an unknown watch id says so rather than staying silent.
Run-DebuggerTest "unwatch_missing_id" @(
    "b debugger_eval_test.obs:56",
    "r",
    "watch five",
    "unwatch 42",
    "watches"
) @("no watchpoint #42", "watch #1") $EvalBin

# Test 29: a string literal prints. It used to evaluate to nothing at all --
# 'p "text"' returned silently, printing no line whatsoever.
Run-DebuggerTest "print_string_literal" @(
    "b debugger_eval_test.obs:56",
    "r",
    "p ""widget"""
) @('print: type=System.String, value="widget"') $EvalBin

# Test 30: a String variable compares equal to its own text.
#
# Asserted as a SINGLE true, not a mixture of true and false. The pre-fix build
# compared the string variable's pointer against the literal's zero, so any test
# that accepted both answers passed on it for the wrong reason -- '<>' against a
# non-null pointer is true there too.
Run-DebuggerTest "string_comparison_equal" @(
    "b debugger_eval_test.obs:56",
    "r",
    "p text = ""widget"""
) @("print: type=Bool, value=true") $EvalBin

# Test 30b: and unequal text compares false, which the pointer comparison also
# could not produce -- it answered false for BOTH.
Run-DebuggerTest "string_comparison_unequal" @(
    "b debugger_eval_test.obs:56",
    "r",
    "p text <> ""widget"""
) @("print: type=Bool, value=false") $EvalBin

# Test 30c: the payoff -- a breakpoint conditional on a string. This was simply
# not expressible before: the condition compared a pointer to zero, never
# matched, and the program ran to completion without stopping.
Run-DebuggerTest "string_conditional_break" @(
    "b debugger_eval_test.obs:58 if text = ""widget""",
    "r",
    "p five"
) @("break: file='debugger_eval_test.obs:58'", "value=5") $EvalBin

# Test 31: true/false are literals. MakeBooleanLiteral had no caller, so these
# words were looked up as variables named "true" and "false".
Run-DebuggerTest "boolean_literals" @(
    "b debugger_eval_test.obs:56",
    "r",
    "p true",
    "p false"
) @("print: type=Bool, value=true", "print: type=Bool, value=false") $EvalBin

# Test 32: Nil is a literal, so an object can be tested for it.
Run-DebuggerTest "nil_literal" @(
    "b debugger_eval_test.obs:56",
    "r",
    "p empty = Nil",
    "p holder = Nil",
    "p holder <> Nil"
) @("print: type=Bool, value=true", "print: type=Bool, value=false") $EvalBin

# Test 33: printing an object lists its fields instead of bottoming out at a
# hex address that told the reader nothing.
Run-DebuggerTest "print_object_fields" @(
    "b debugger_eval_test.obs:56",
    "r",
    "p holder"
) @("print: type=Holder", "@name = ""widget""", "@count = 7", "@ratio = 1.5", "@next = Nil") $EvalBin

# Test 34: 'set' refuses a value it cannot store. It used to evaluate the string
# to nothing, store the resulting zero, and report 'set: value=0(0x0)'.
Run-DebuggerTest "set_rejects_string" @(
    "b debugger_eval_test.obs:56",
    "r",
    "set five = ""oops""",
    "p five"
) @("cannot set: only Int, Char and Float", "value=5") $EvalBin

# Test 35: a zero numerator is valid modulus. The old guard tested the operands
# for truthiness, so '0 % 5' was rejected as "requires integer values".
Run-DebuggerTest "modulus_zero_numerator" @(
    "b debugger_eval_test.obs:56",
    "r",
    "p zero % five"
) @("print: type=Int, value=0") $EvalBin

# Test 36: division and modulus by zero are reported, not executed. Integer
# division by zero had no guard at all and faulted the debugger process.
Run-DebuggerTest "divide_by_zero" @(
    "b debugger_eval_test.obs:56",
    "r",
    "p five / zero",
    "p five % zero",
    "p five"
) @("division by zero", "modulus by zero", "value=5") $EvalBin

# Test 37: a class holding only statics shows them. The class-declaration print
# was nested inside the instance-declaration test, so this printed nothing.
Run-DebuggerTest "info_static_only_class" @(
    "b debugger_eval_test.obs:56",
    "r",
    "info class=Registry"
) @("class: type=Registry", "@seen", "@label_text") $EvalBin

# Test 38: a leading space no longer breaks every command. The '?' sentinel was
# concatenated onto the raw line, so " p five" became "? p five" and a lone '?'
# scans as an identifier rather than a keyword.
Run-DebuggerTest "leading_whitespace" @(
    "b debugger_eval_test.obs:56",
    "r",
    "   p five"
) @("value=5") $EvalBin

# ---------------------------------------------------------------------------
# Threads. A spawned VM thread used to run with no debugger attached, so a
# breakpoint inside any thread body never fired: the program ran to
# completion and every later command answered "program is not running".
# Each of these fails on that build.
# ---------------------------------------------------------------------------

# Test 39: a breakpoint inside a thread's Run method fires at all.
Run-DebuggerTest "thread_breakpoint_fires" @(
    "b debugger_thread_test.obs:38",
    "r",
    "c"
) @("break: file='debugger_thread_test.obs:38', method='Worker->Run(..)'") $ThreadBin

# Test 40: the stop belongs to the RIGHT thread. Each worker's 'step' is
# id*100, so a conditional on step = 200 can only be satisfied in worker 2,
# and the frame the prompt evaluates against must be that worker's.
Run-DebuggerTest "thread_conditional_stops_right_thread" @(
    "b debugger_thread_test.obs:38 if step = 200",
    "r",
    "p step",
    "c"
) @("method='Worker->Run(..)'", "value=200") $ThreadBin

# Test 41: 'threads' lists the real threads -- Main plus the workers -- and
# marks the one that owns the stop. There was no such command before.
Run-DebuggerTest "threads_lists_workers" @(
    "b debugger_thread_test.obs:38",
    "r",
    "threads",
    "c"
) @("thread #1: ThreadDebugTest->Main", "Worker->Run", "(stopped here)") $ThreadBin

Write-Host ""
Write-Host "========================================"
Write-Host "  Results: $PassCount passed, $FailCount failed"
Write-Host "========================================"

if ($FailCount -gt 0) { exit 1 } else { exit 0 }
