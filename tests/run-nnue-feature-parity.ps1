param(
    [string]$ThetaProbe = "build\theta_feature_probe.exe",
    [string]$StockfishProbe = "build\stockfish_feature_probe"
)

$ErrorActionPreference = "Stop"
$thetaPath = (Resolve-Path $ThetaProbe).Path
$stockfishPath = $StockfishProbe.Replace("\", "/")
$fens = @(
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 1 3",
    "r3k2r/8/8/3pP3/8/8/8/R3K2R w KQkq d6 0 1",
    "4k3/P7/8/8/8/8/8/4K3 w - - 0 1",
    "r2q1rk1/ppp2ppp/2n1b3/3np3/3NP3/2N1B3/PPP2PPP/R2Q1RK1 w - - 4 10",
    "8/2p5/3p4/1P1Pp3/4P3/8/8/4K2k b - - 2 35",
    "2r2rk1/1bqnbppp/p3p3/pp1pP3/3P1N2/2N1B3/PPPQBPPP/2R2RK1 w - - 0 15",
    "6k1/pp3ppp/2p5/3pP3/3P4/2P3P1/PP3P1P/6K1 w - - 0 30"
)

function Get-FeatureLines([string[]]$lines) {
    return @($lines | Where-Object { $_ -match "^(psq|threats|pairs) " })
}

foreach ($fen in $fens) {
    $thetaLines = @(Get-FeatureLines (& $thetaPath $fen))
    $stockfishLines = @(Get-FeatureLines (& wsl bash -lc "cd /mnt/c/Users/FM/Downloads/theta && $stockfishPath '$fen'"))
    if ($thetaLines.Count -ne $stockfishLines.Count) {
        throw "Feature line count mismatch for FEN: $fen"
    }
    for ($index = 0; $index -lt $thetaLines.Count; ++$index) {
        if ($thetaLines[$index] -ne $stockfishLines[$index]) {
            throw "Feature mismatch for FEN: $fen`nTheta: $($thetaLines[$index])`nStockfish: $($stockfishLines[$index])"
        }
    }
    Write-Output "parity ok: $fen"
}

Write-Output "NNUE feature parity passed: $($fens.Count) positions"
