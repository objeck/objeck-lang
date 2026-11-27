<#
.SYNOPSIS
    Does this machine's loopback answer every connect? A plain .NET probe, no Objeck involved.

.DESCRIPTION
    A regression test that talks to its own server over 127.0.0.1 can fail on one Windows
    machine and pass on every other one because of the machine, not the test: a connect whose
    SYN is never answered gives up after about 21 seconds (three SYNs, 3 + 6 + 12 s), and the
    request it was for fails. On the development machine that first showed it, the cause was a
    filter driver in the loopback path (docs/windows_loopback_sockets.md, issue 1443); GitHub's
    hosted Windows runners do not do it.

    This script answers the question for the machine it runs on, with nothing of Objeck's in
    the picture: a .NET listener and .NET clients, in this one process.

      1. What sits on the network path: OS build, non-Microsoft kernel drivers that are
         running, the dynamic port range, the SYN retransmission settings, WSL's networking mode.
      2. The probe, once against a dual-stack wildcard listener ([::] with IPv4 mapped, what
         an Objeck server binds by default) and once against a listener on 127.0.0.1 only:
         -Connections connects from -Threads threads to 127.0.0.1, each sending a line,
         reading the reply and closing first, as an Objeck client does. A connect that is not
         answered within -StallMs is a stall. The stalled socket is left to finish, to see
         whether a later SYN gets through (3 s, 9 s) or none does (about 21 s), and the
         request is tried again at once on a new socket, to see whether only that one
         connection (its source port) was affected.

    Read the last lines. "stalled=0" in both modes, on a run of several thousand connects,
    says the loopback is sound and a 21 second connect in a test is the test's or the
    library's to explain. Stalls here say it is the machine: nothing of Objeck's ran.

    It changes nothing on the machine and needs no elevation. It makes 2 x -Connections
    connections that linger in TIME_WAIT for two minutes, so keep -Connections under about
    6000, and do not run a network test suite beside it.

    The verdict is per listener: the exit status is 0 only if every requested connection was
    answered (ok equals -Connections and the server served that many) with no stall and no
    error. -SelfTest checks that rule against sample reports, with no sockets, and exits.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\cicd\loopback_connect_probe.ps1
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\cicd\loopback_connect_probe.ps1 -Connections 6000 -Threads 6
#>
[CmdletBinding()]
param(
    [int] $Connections = 4000,
    [int] $Threads = 6,
    [int] $StallMs = 1000,
    [switch] $SkipInventory,
    [switch] $SelfTest
)

$ErrorActionPreference = 'Stop'

$source = @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;

public static class LoopbackConnectProbe
{
    sealed class Stall
    {
        public Socket Socket;
        public IAsyncResult Pending;
        public Stopwatch Watch;
        public int LocalPort;
        public long RetryMs;
        public string RetryResult;
    }

    static int served;

    static void Serve(object state)
    {
        Socket client = (Socket)state;
        try
        {
            client.ReceiveTimeout = 30000;
            byte[] buffer = new byte[256];
            int read = client.Receive(buffer);
            if (read > 0)
            {
                // counted before the reply, so a client that has read its reply is always counted
                Interlocked.Increment(ref served);
                client.Send(Encoding.ASCII.GetBytes("HTTP/1.1 200 OK\r\n\r\n"));
                // the client closes first, as an Objeck HTTP client does: wait for it
                while (client.Receive(buffer) > 0) { }
            }
        }
        catch (SocketException) { }
        catch (ObjectDisposedException) { }
        finally
        {
            try { client.Shutdown(SocketShutdown.Send); } catch (SocketException) { } catch (ObjectDisposedException) { }
            client.Close();
        }
    }

    static string Exchange(Socket socket)
    {
        socket.ReceiveTimeout = 30000;
        socket.Send(Encoding.ASCII.GetBytes("GET / HTTP/1.1\r\n\r\n"));
        byte[] buffer = new byte[64];
        int read = socket.Receive(buffer);
        socket.Shutdown(SocketShutdown.Send);
        socket.Close();
        return read > 0 ? "ok" : "no reply";
    }

    public static string Run(bool dualStack, int connections, int threads, int stallMs)
    {
        Socket listener;
        if (dualStack)
        {
            listener = new Socket(AddressFamily.InterNetworkV6, SocketType.Stream, ProtocolType.Tcp);
            listener.SetSocketOption(SocketOptionLevel.IPv6, SocketOptionName.IPv6Only, false);
            listener.Bind(new IPEndPoint(IPAddress.IPv6Any, 0));
        }
        else
        {
            listener = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
            listener.Bind(new IPEndPoint(IPAddress.Loopback, 0));
        }
        listener.Listen(128);
        int port = ((IPEndPoint)listener.LocalEndPoint).Port;
        served = 0;

        Thread acceptor = new Thread(delegate ()
        {
            try
            {
                while (true)
                {
                    Socket client = listener.Accept();
                    ThreadPool.QueueUserWorkItem(Serve, client);
                }
            }
            catch (SocketException) { }
            catch (ObjectDisposedException) { }
        });
        acceptor.IsBackground = true;
        acceptor.Start();

        object gate = new object();
        List<Stall> stalls = new List<Stall>();
        List<string> errors = new List<string>();
        int ok = 0;
        long slowestMs = 0;
        Stopwatch total = Stopwatch.StartNew();

        Thread[] workers = new Thread[threads];
        for (int t = 0; t < threads; t++)
        {
            int share = connections / threads + (t < connections % threads ? 1 : 0);
            workers[t] = new Thread(delegate ()
            {
                for (int i = 0; i < share; i++)
                {
                    Socket socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
                    Stopwatch watch = Stopwatch.StartNew();
                    try
                    {
                        IAsyncResult pending = socket.BeginConnect(new IPEndPoint(IPAddress.Loopback, port), null, null);
                        if (!pending.AsyncWaitHandle.WaitOne(stallMs))
                        {
                            // not answered: leave it to finish, and try again on a new socket
                            Stall stall = new Stall();
                            stall.Socket = socket;
                            stall.Pending = pending;
                            stall.Watch = watch;
                            try { stall.LocalPort = ((IPEndPoint)socket.LocalEndPoint).Port; } catch (Exception) { }
                            Stopwatch retryWatch = Stopwatch.StartNew();
                            try
                            {
                                Socket retry = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
                                IAsyncResult retryPending = retry.BeginConnect(new IPEndPoint(IPAddress.Loopback, port), null, null);
                                if (retryPending.AsyncWaitHandle.WaitOne(stallMs))
                                {
                                    retry.EndConnect(retryPending);
                                    stall.RetryResult = Exchange(retry);
                                }
                                else
                                {
                                    stall.RetryResult = "stalled too";
                                    retry.Close();
                                }
                            }
                            catch (SocketException e) { stall.RetryResult = "error " + e.ErrorCode; }
                            stall.RetryMs = retryWatch.ElapsedMilliseconds;
                            lock (gate) { stalls.Add(stall); }
                            continue;
                        }
                        socket.EndConnect(pending);
                        long ms = watch.ElapsedMilliseconds;
                        string result = Exchange(socket);
                        lock (gate)
                        {
                            if (result == "ok") { ok++; } else { errors.Add(result); }
                            if (ms > slowestMs) { slowestMs = ms; }
                        }
                    }
                    catch (SocketException e)
                    {
                        lock (gate) { errors.Add("error " + e.ErrorCode + " after " + watch.ElapsedMilliseconds + " ms"); }
                        try { socket.Close(); } catch (Exception) { }
                    }
                }
            });
            workers[t].Start();
        }
        foreach (Thread worker in workers) { worker.Join(); }
        long clientMs = total.ElapsedMilliseconds;

        // what became of each stalled connect: a later SYN answered, or none (about 21 s)
        StringBuilder detail = new StringBuilder();
        foreach (Stall stall in stalls)
        {
            string outcome;
            if (stall.Pending.AsyncWaitHandle.WaitOne(30000))
            {
                try
                {
                    stall.Socket.EndConnect(stall.Pending);
                    outcome = "connected after " + stall.Watch.ElapsedMilliseconds + " ms";
                }
                catch (SocketException e)
                {
                    outcome = "failed with " + e.ErrorCode + " after " + stall.Watch.ElapsedMilliseconds + " ms";
                }
            }
            else
            {
                outcome = "still pending after " + stall.Watch.ElapsedMilliseconds + " ms";
            }
            try { stall.Socket.Close(); } catch (Exception) { }
            detail.AppendLine("    stall: source port " + stall.LocalPort + ": " + outcome +
                              "; the retry on a new socket: " + stall.RetryResult + " in " + stall.RetryMs + " ms");
        }

        listener.Close();

        StringBuilder report = new StringBuilder();
        report.AppendLine((dualStack ? "dual-stack [::] listener" : "127.0.0.1 listener") + ", port " + port + ": connects=" + connections +
                          " ok=" + ok + " stalled=" + stalls.Count + " errors=" + errors.Count + " served=" + served +
                          " slowest_answered_connect_ms=" + slowestMs + " elapsed_ms=" + clientMs);
        report.Append(detail.ToString());
        int shown = 0;
        foreach (string error in errors)
        {
            if (shown++ < 10) { report.AppendLine("    error: " + error); }
        }
        return report.ToString();
    }
}
'@

# What is wrong with one listener's report line, or $null. A stall count alone is not the
# verdict: a listener that refused or reset every connect at once has no stalls either.
function Get-ProbeProblem([string] $report, [int] $connections) {
    if ($report -notmatch 'connects=(\d+) ok=(\d+) stalled=(\d+) errors=(\d+) served=(\d+)') {
        return 'the report has no counts to read'
    }
    $c = [int]$Matches[1]; $ok = [int]$Matches[2]; $st = [int]$Matches[3]; $er = [int]$Matches[4]; $sv = [int]$Matches[5]
    $found = @()
    if ($c -ne $connections) { $found += ("{0} connects were made, {1} requested" -f $c, $connections) }
    if ($st -ne 0) { $found += ("{0} stalled" -f $st) }
    if ($er -ne 0) { $found += ("{0} failed with an error" -f $er) }
    if ($ok -ne $connections) { $found += ("{0} of {1} exchanges completed" -f $ok, $connections) }
    if ($sv -ne $connections) { $found += ("the server served {0} of {1}" -f $sv, $connections) }
    if ($found.Count -gt 0) { return ($found -join '; ') }
    return $null
}

if ($SelfTest) {
    $cases = @(
        @{ name = 'all answered';          line = 'x: connects=100 ok=100 stalled=0 errors=0 served=100'; bad = $false },
        @{ name = 'every connect errored'; line = 'x: connects=100 ok=0 stalled=0 errors=100 served=0';   bad = $true },
        @{ name = 'some errors';           line = 'x: connects=100 ok=99 stalled=0 errors=1 served=99';  bad = $true },
        @{ name = 'a stall';               line = 'x: connects=100 ok=99 stalled=1 errors=0 served=100'; bad = $true },
        @{ name = 'nothing was served';    line = 'x: connects=100 ok=100 stalled=0 errors=0 served=0';  bad = $true },
        @{ name = 'fewer connects made';   line = 'x: connects=50 ok=50 stalled=0 errors=0 served=50';   bad = $true },
        @{ name = 'unreadable';            line = 'no counts here';                                      bad = $true }
    )
    $wrong = 0
    foreach ($case in $cases) {
        $problem = Get-ProbeProblem $case.line 100
        $failed = $null -ne $problem
        $verdict = 'ok  '
        if ($failed -ne $case.bad) { $verdict = 'WRONG'; $wrong++ }
        $shown = 'passes'
        if ($problem) { $shown = $problem }
        Write-Host ("  {0} {1}: {2}" -f $verdict, $case.name, $shown)
    }
    if ($wrong -gt 0) { Write-Host ("SELFTEST: {0} case(s) judged wrongly" -f $wrong); exit 1 }
    Write-Host 'SELFTEST: ok'
    exit 0
}

Write-Host ''
Write-Host 'Loopback connect probe'
Write-Host ('=' * 60)

if (-not $SkipInventory) {
    $os = Get-CimInstance Win32_OperatingSystem
    Write-Host ("OS: {0} build {1}, {2}" -f $os.Caption, $os.BuildNumber, $env:PROCESSOR_ARCHITECTURE)

    Write-Host ''
    Write-Host 'Running kernel drivers that are not Microsoft''s (a filter on the network path would be here):'
    $drivers = Get-CimInstance Win32_SystemDriver -Filter "State='Running'" -ErrorAction SilentlyContinue
    $listed = 0
    foreach ($driver in $drivers) {
        $path = $driver.PathName
        if (-not $path) { continue }
        $path = $path -replace '^\\\?\?\\', '' -replace '^\\SystemRoot', $env:SystemRoot
        if ($path -notmatch '^[A-Za-z]:') { $path = Join-Path $env:SystemRoot $path }
        $company = ''
        try { $company = (Get-Item -LiteralPath $path -ErrorAction Stop).VersionInfo.CompanyName } catch { }
        if ($company -notmatch 'Microsoft') {
            Write-Host ("  {0,-28} {1,-34} {2}" -f $driver.Name, $company, $driver.DisplayName)
            $listed++
        }
    }
    if ($listed -eq 0) { Write-Host '  (none)' }

    Write-Host ''
    Write-Host 'TCP settings:'
    try {
        netsh int ipv4 show dynamicport tcp | Where-Object { $_ -match '\d' } | ForEach-Object { Write-Host "  $($_.Trim())" }
    } catch { }
    try {
        Get-NetTCPSetting -ErrorAction Stop | Where-Object { $_.SettingName -in 'Internet', 'InternetCustom', 'Datacenter' } |
            ForEach-Object { Write-Host ("  {0}: InitialRto={1} ms, MaxSynRetransmissions={2}" -f $_.SettingName, $_.InitialRto, $_.MaxSynRetransmissions) }
    } catch { Write-Host '  (Get-NetTCPSetting is not available)' }

    $wslconfig = Join-Path $env:USERPROFILE '.wslconfig'
    if (Test-Path $wslconfig) {
        $mode = Select-String -Path $wslconfig -Pattern '^\s*networkingMode\s*=' -ErrorAction SilentlyContinue
        if ($mode) { Write-Host ("  WSL: {0}" -f $mode.Line.Trim()) } else { Write-Host '  WSL: .wslconfig sets no networkingMode (NAT)' }
    } else {
        Write-Host '  WSL: no .wslconfig'
    }
    Write-Host ''
}

Add-Type -TypeDefinition $source -Language CSharp

Write-Host ("Probe: {0} connects from {1} threads per listener; a connect not answered in {2} ms is a stall" -f $Connections, $Threads, $StallMs)
$stalled = 0
$problems = @()
foreach ($dual in @($true, $false)) {
    $report = [LoopbackConnectProbe]::Run($dual, $Connections, $Threads, $StallMs)
    Write-Host $report
    if ($report -match 'stalled=(\d+)') { $stalled += [int]$Matches[1] }
    $problem = Get-ProbeProblem $report $Connections
    if ($problem) {
        $which = '127.0.0.1'
        if ($dual) { $which = 'dual-stack' }
        $problems += ("{0} listener: {1}" -f $which, $problem)
    }
}

if ($stalled -gt 0) {
    Write-Host ("RESULT: {0} connect(s) to this machine's own loopback went unanswered, with nothing of Objeck's running." -f $stalled)
}
if ($problems.Count -gt 0) {
    foreach ($problem in $problems) { Write-Host ("RESULT: {0}" -f $problem) }
    Write-Host 'RESULT: the loopback did not answer every request: see the lines above.'
    exit 1
}
Write-Host ('RESULT: all {0} loopback connects to each listener were answered, with no stall and no error.' -f $Connections)
exit 0
