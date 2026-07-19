param(
    [int]$Games = 14,
    [string]$TimeControl = "2+0.02",
    [int]$StockfishElo = 1320,
    [int]$Concurrency = 1,
    [int]$HashMb = 16,
    [int]$TimeMarginMs = 250,
    [ValidateSet("sequential", "random")]
    [string]$OpeningOrder = "random"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$theta = Join-Path $root "build\theta.exe"
$stockfish = Join-Path $PSScriptRoot "stockfish-18.exe"
$openings = Join-Path $PSScriptRoot "cutechess-openings.epd"
$cutechess = Get-ChildItem -LiteralPath $PSScriptRoot -Filter "cutechess-cli.exe" -Recurse |
    Select-Object -First 1 -ExpandProperty FullName

if (-not (Test-Path -LiteralPath $theta)) {
    throw "Theta is not built. Run build.cmd first."
}
if (-not (Test-Path -LiteralPath $stockfish)) {
    throw "Missing Stockfish executable: $stockfish"
}
if (-not $cutechess) {
    throw "cutechess-cli.exe was not found below $PSScriptRoot"
}
if ($Games -lt 2 -or ($Games % 2) -ne 0) {
    throw "Games must be a positive even number so colors and openings are paired."
}

$results = Join-Path $PSScriptRoot "results"
New-Item -ItemType Directory -Path $results -Force | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$pgn = Join-Path $results "theta-vs-stockfish-$stamp.pgn"
$log = Join-Path $results "theta-vs-stockfish-$stamp.log"

$arguments = @(
    "-engine", "name=Theta", "cmd=$theta", "dir=$root", "proto=uci", "restart=off",
    "-engine", "name=Stockfish18-Limited", "cmd=$stockfish", "dir=$root", "proto=uci",
        "restart=off", "option.Threads=1", "option.Hash=$HashMb",
        "option.UCI_LimitStrength=true", "option.UCI_Elo=$StockfishElo",
    "-each", "tc=$TimeControl", "timemargin=$TimeMarginMs",
    "-openings", "file=$openings", "format=epd", "order=$OpeningOrder",
    "-games", "$Games", "-rounds", "1", "-repeat",
    "-concurrency", "$Concurrency",
    "-resign", "movecount=3", "score=800",
    "-draw", "movenumber=80", "movecount=8", "score=10",
    "-maxmoves", "160", "-recover", "-ratinginterval", "2",
    "-pgnout", $pgn, "fi"
)

"Cute Chess: $cutechess" | Tee-Object -FilePath $log
"Settings: games=$Games tc=$TimeControl concurrency=$Concurrency hash=${HashMb}MB stockfishElo=$StockfishElo openings=$OpeningOrder timemargin=${TimeMarginMs}ms" |
    Tee-Object -FilePath $log -Append
& $cutechess @arguments 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) {
    throw "Cute Chess exited with code $LASTEXITCODE. See $log"
}

"PGN: $pgn" | Tee-Object -FilePath $log -Append
"Log: $log"
