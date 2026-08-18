param(
    [string]$InputPath = "build\classical-selfplay.txt",
    [string]$OutputPath = "build\stockfish-labelled-classical.txt",
    [string]$StockfishPath = "test\stockfish-18.exe",
    [int]$Depth = 8,
    [int]$MaxPositions = 10000,
    [int]$SkipPositions = 0
)

$ErrorActionPreference = "Stop"

if ($Depth -lt 1) {
    throw "Depth must be positive."
}
if ($MaxPositions -lt 1) {
    throw "MaxPositions must be positive."
}
if ($SkipPositions -lt 0) {
    throw "SkipPositions cannot be negative."
}

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$inputFile = (Resolve-Path (Join-Path $root $InputPath)).Path
$stockfish = (Resolve-Path (Join-Path $root $StockfishPath)).Path
$outputFile = Join-Path $root $OutputPath
$outputDirectory = Split-Path -Parent $outputFile

if (-not (Test-Path -LiteralPath $outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

$allFens = New-Object 'System.Collections.Generic.List[string]'
$seen = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($line in Get-Content -LiteralPath $inputFile) {
    $trimmed = $line.Trim()
    $separator = $trimmed.IndexOfAny([char[]]" `t")
    if ($separator -lt 0) {
        continue
    }

    $fen = $trimmed.Substring($separator + 1).Trim()
    if ($fen -and $seen.Add($fen)) {
        $allFens.Add($fen)
    }
}

$fens = @(
    $allFens | Select-Object -Skip $SkipPositions -First $MaxPositions
)

if ($fens.Count -eq 0) {
    throw "No FENs found in $inputFile."
}

$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = $stockfish
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardInput = $true
$startInfo.RedirectStandardOutput = $true
$process = New-Object System.Diagnostics.Process
$process.StartInfo = $startInfo
[void]$process.Start()
$writer = $process.StandardInput
$reader = $process.StandardOutput
$writer.AutoFlush = $true

function Read-Until([string]$pattern) {
    while (($line = $reader.ReadLine()) -ne $null) {
        if ($line -match $pattern) {
            return $line
        }
    }
    throw "Stockfish exited before emitting $pattern."
}

try {
    $writer.WriteLine("uci")
    [void](Read-Until "^uciok$")
    $writer.WriteLine("setoption name Threads value 1")
    $writer.WriteLine("setoption name Hash value 64")
    $writer.WriteLine("isready")
    [void](Read-Until "^readyok$")

    $culture = [Globalization.CultureInfo]::InvariantCulture
    $output = New-Object System.IO.StreamWriter($outputFile, $false)
    $output.WriteLine("# Stockfish-labelled Texel dataset")
    $output.WriteLine("# depth=$Depth positions=$($fens.Count)")

    for ($index = 0; $index -lt $fens.Count; ++$index) {
        $fen = $fens[$index]
        $fields = $fen -split "\s+"
        $writer.WriteLine("position fen $fen")
        $writer.WriteLine("go depth $Depth")

        $score = $null
        while (($line = $reader.ReadLine()) -ne $null) {
            if ($line -match "^info .* score cp (-?\d+)") {
                $score = [int]$Matches[1]
            } elseif ($line -match "^info .* score mate (-?\d+)") {
                $score = if ([int]$Matches[1] -gt 0) { 3000 } else { -3000 }
            }
            if ($line -match "^bestmove ") {
                break
            }
        }

        if ($null -eq $score) {
            continue
        }

        $whiteScore = if ($fields.Count -gt 1 -and $fields[1] -eq "b") {
            -$score
        } else {
            $score
        }
        $probability = if ($whiteScore -gt 2400) {
            0.9975
        } elseif ($whiteScore -lt -2400) {
            0.0025
        } else {
            1.0 / (1.0 + [Math]::Exp(-$whiteScore / 400.0))
        }

        $output.WriteLine(
            ($probability.ToString("F6", $culture) + " " + $fen)
        )
        if ((($index + 1) % 250) -eq 0 -or $index + 1 -eq $fens.Count) {
            Write-Output "labelled $($index + 1)/$($fens.Count)"
        }
    }
    $output.Close()
    Write-Output "dataset $outputFile rows $($seen.Count)"
} finally {
    if ($null -ne $output -and -not $output.BaseStream -eq $null) {
        $output.Dispose()
    }
    if ($null -ne $process -and -not $process.HasExited) {
        $writer.WriteLine("quit")
        $process.WaitForExit(2000) | Out-Null
        if (-not $process.HasExited) {
            $process.Kill()
        }
    }
    if ($null -ne $process) {
        $process.Dispose()
    }
}
