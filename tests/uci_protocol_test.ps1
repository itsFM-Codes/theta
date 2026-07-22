param(
    [string]$EnginePath = "build\theta.exe"
)

$ErrorActionPreference = "Stop"
$engine = (Resolve-Path -LiteralPath $EnginePath).Path
$process = New-Object System.Diagnostics.Process
$process.StartInfo.FileName = $engine
$process.StartInfo.WorkingDirectory = (Get-Location).Path
$process.StartInfo.UseShellExecute = $false
$process.StartInfo.RedirectStandardInput = $true
$process.StartInfo.RedirectStandardOutput = $true
$process.StartInfo.RedirectStandardError = $true
$process.StartInfo.CreateNoWindow = $true

function Send-Command([string]$Command) {
    $process.StandardInput.WriteLine($Command)
    $process.StandardInput.Flush()
}

function Read-Until([scriptblock]$Predicate, [int]$TimeoutMs = 10000) {
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $seen = New-Object System.Collections.Generic.List[string]

    while ($timer.ElapsedMilliseconds -lt $TimeoutMs) {
        $remaining = $TimeoutMs - [int]$timer.ElapsedMilliseconds
        $read = $process.StandardOutput.ReadLineAsync()
        if (-not $read.Wait([Math]::Max(1, $remaining))) {
            break
        }
        $line = $read.Result
        if ($null -eq $line) {
            break
        }
        $seen.Add($line)
        if (& $Predicate $line) {
            return @{ Line = $line; Seen = $seen }
        }
    }

    $state = "Engine is still running."
    if ($process.HasExited) {
        $stderr = $process.StandardError.ReadToEnd().Trim()
        $state = "Engine exited with code $($process.ExitCode). stderr: $stderr"
    }

    throw "Timed out waiting for engine output. Saw: $($seen -join ' | '). $state"
}

function Read-BestMove([int]$TimeoutMs = 3000) {
    $result = Read-Until { param($line) $line.StartsWith("bestmove ") } $TimeoutMs
    if ($result.Line -notmatch '^bestmove ([a-h][1-8][a-h][1-8][qrbn]?|0000)$') {
        throw "Invalid bestmove response: $($result.Line)"
    }
    return $result
}

try {
    if (-not $process.Start()) {
        throw "Could not start engine"
    }

    Send-Command "uci"
    $uci = Read-Until { param($line) $line -eq "uciok" }
    if (-not ($uci.Seen | Where-Object { $_ -like "option name Hash *" })) {
        throw "UCI Hash option was not reported"
    }
    if (-not ($uci.Seen | Where-Object { $_ -eq "option name Clear Hash type button" })) {
        throw "UCI Clear Hash option was not reported"
    }
    if (-not ($uci.Seen | Where-Object { $_ -eq "option name Allow Draws type check default true" })) {
        throw "UCI Allow Draws option was not reported"
    }
    Send-Command "setoption name Hash value 4"
    Send-Command "setoption name Clear Hash"
    Send-Command "isready"
    $null = Read-Until { param($line) $line -eq "readyok" }

    Send-Command "position startpos"
    Send-Command "go infinite"
    $null = Read-Until { param($line) $line.StartsWith("info depth ") }
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    Send-Command "stop"
    $null = Read-BestMove 2000
    if ($timer.ElapsedMilliseconds -ge 1000) {
        throw "stop took $($timer.ElapsedMilliseconds) ms"
    }

    Send-Command "position startpos"
    $timer.Restart()
    Send-Command "go movetime 120"
    $null = Read-BestMove 2000
    if ($timer.ElapsedMilliseconds -lt 20 -or $timer.ElapsedMilliseconds -ge 400) {
        throw "movetime search took $($timer.ElapsedMilliseconds) ms"
    }

    Send-Command "position startpos"
    Send-Command "go nodes 100"
    $null = Read-BestMove 2000

    Send-Command "position startpos moves g1f3 g8f6 f3g1 f6g8 g1f3 g8f6 f3g1 f6g8"
    Send-Command "go depth 1"
    $result = Read-BestMove 2000
    if (-not ($result.Seen | Where-Object { " $_ " -like "* score cp 0 *" })) {
        throw "Repeated position was not scored as a draw: $($result.Seen -join ' | ')"
    }

    Send-Command "setoption name Allow Draws value false"
    Send-Command "position startpos moves g1f3 g8f6 f3g1 f6g8 g1f3 g8f6 f3g1 f6g8"
    Send-Command "go depth 1"
    $result = Read-BestMove 2000
    if (-not ($result.Seen | Where-Object { " $_ " -like "* score cp 1000 *" })) {
        throw "Draw avoidance score was not applied: $($result.Seen -join ' | ')"
    }
}
finally {
    if (-not $process.HasExited) {
        Send-Command "quit"
        if (-not $process.WaitForExit(3000)) {
            $process.Kill()
        }
    }
    $process.Dispose()
}
