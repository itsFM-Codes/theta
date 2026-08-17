param(
    [int]$Iterations = 3,
    [int]$GamesPerCandidate = 8,
    [string]$TimeControl = "2+0.02",
    [int]$StockfishElo = 2800,
    [int]$HashMb = 16,
    [int]$TimeMarginMs = 250,
    [string]$ThetaNnueFile = "",
    [ValidateSet("true", "false")]
    [string]$ThetaUseNnue = "false",
    [int]$Seed = 20260805,
    [string]$OutputPath = "build\spsa-search-tuning.csv"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$matchScript = Join-Path $PSScriptRoot "run-cutechess-match.ps1"

if ($Iterations -lt 1) {
    throw "Iterations must be positive."
}
if ($GamesPerCandidate -lt 2 -or ($GamesPerCandidate % 2) -ne 0) {
    throw "GamesPerCandidate must be a positive even number."
}

$parameters = @(
    [pscustomobject]@{
        Name = "lmrDepth"
        Uci = "Search LMR Depth Start"
        Value = 3
        Minimum = 2
        Maximum = 6
        A = 1.0
        C = 1.0
    },
    [pscustomobject]@{
        Name = "lmrMove"
        Uci = "Search LMR Move Start"
        Value = 3
        Minimum = 2
        Maximum = 8
        A = 1.0
        C = 1.0
    },
    [pscustomobject]@{
        Name = "nullBase"
        Uci = "Search Null Move Base"
        Value = 4
        Minimum = 1
        Maximum = 8
        A = 1.0
        C = 1.0
    },
    [pscustomobject]@{
        Name = "futility"
        Uci = "Search Static Futility Margin"
        Value = 105
        Minimum = 40
        Maximum = 200
        A = 10.0
        C = 10.0
    }
)

function Get-ParameterValues {
    param([object[]]$Items)

    $values = @{}
    foreach ($item in $Items) {
        $values[$item.Name] = [int]$item.Value
    }
    return $values
}

function Get-OptionArguments {
    param([object[]]$Items)

    $options = @()
    foreach ($item in $Items) {
        $options += "$($item.Uci)=$([int]$item.Value)"
    }
    return ,$options
}

function Set-ParameterValue {
    param(
        [object]$Item,
        [double]$Value
    )

    $rounded = [int][math]::Round($Value, 0, [MidpointRounding]::AwayFromZero)
    if ($rounded -lt $Item.Minimum) {
        $rounded = $Item.Minimum
    }
    if ($rounded -gt $Item.Maximum) {
        $rounded = $Item.Maximum
    }
    $Item.Value = $rounded
}

function Invoke-TuningMatch {
    param(
        [object[]]$Items,
        [string]$Label
    )

    $options = Get-OptionArguments $Items
    $arguments = @(
        "-Games", "$GamesPerCandidate",
        "-TimeControl", $TimeControl,
        "-StockfishElo", "$StockfishElo",
        "-Concurrency", "1",
        "-HashMb", "$HashMb",
        "-TimeMarginMs", "$TimeMarginMs",
        "-OpeningOrder", "sequential",
        "-ThetaUseNnue", $ThetaUseNnue,
        "-ThetaOptions", ($options -join ",")
    )
    if ($ThetaNnueFile) {
        $arguments += @("-ThetaNnueFile", $ThetaNnueFile)
    }
    Write-Host "SPSA match ${Label}: $($options -join ', ')"
    $output = & powershell -NoProfile -ExecutionPolicy Bypass `
        -File $matchScript @arguments 2>&1
    $exitCode = $LASTEXITCODE
    $lines = @($output | ForEach-Object { $_.ToString() })
    if ($exitCode -ne 0) {
        throw "SPSA match $Label failed: $($lines -join "`n")"
    }

    $scoreLine = $lines |
        Where-Object { $_ -match '^Score of Theta vs Stockfish18-Limited:' } |
        Select-Object -Last 1
    if (-not $scoreLine -or
        $scoreLine -notmatch '\[([0-9]+(?:\.[0-9]+)?)\]') {
        throw "Could not parse SPSA score for $Label. Output: $($lines -join "`n")"
    }

    $score = [double]$Matches[1]
    Write-Host "SPSA result $Label score=$score"
    return $score
}

$outputDirectory = Split-Path -Parent $OutputPath
if ($outputDirectory) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}
"iteration,label,score,lmrDepth,lmrMove,nullBase,futility" |
    Set-Content -LiteralPath $OutputPath

$random = New-Object System.Random($Seed)
$theta = Get-ParameterValues $parameters
$baselineScore = Invoke-TuningMatch $parameters "baseline"
"0,baseline,$baselineScore,$($theta.lmrDepth),$($theta.lmrMove),$($theta.nullBase),$($theta.futility)" |
    Add-Content -LiteralPath $OutputPath
$bestScore = $baselineScore
$bestValues = @{} + $theta

for ($iteration = 0; $iteration -lt $Iterations; ++$iteration) {
    $plusItems = @()
    $minusItems = @()
    $directions = @{}
    $stepScale = 1.0 / [math]::Pow($iteration + 1, 0.101)

    foreach ($item in $parameters) {
        $direction = if ($random.Next(2) -eq 0) { -1 } else { 1 }
        $directions[$item.Name] = $direction

        $plusItem = $item.PSObject.Copy()
        $minusItem = $item.PSObject.Copy()
        Set-ParameterValue $plusItem ($theta[$item.Name] + $direction * $item.C * $stepScale)
        Set-ParameterValue $minusItem ($theta[$item.Name] - $direction * $item.C * $stepScale)
        $plusItems += $plusItem
        $minusItems += $minusItem
    }

    $plusScore = Invoke-TuningMatch $plusItems "iteration-$($iteration + 1)-plus"
    $minusScore = Invoke-TuningMatch $minusItems "iteration-$($iteration + 1)-minus"
    "$(($iteration + 1)),plus,$plusScore,$($plusItems[0].Value),$($plusItems[1].Value),$($plusItems[2].Value),$($plusItems[3].Value)" |
        Add-Content -LiteralPath $OutputPath
    "$(($iteration + 1)),minus,$minusScore,$($minusItems[0].Value),$($minusItems[1].Value),$($minusItems[2].Value),$($minusItems[3].Value)" |
        Add-Content -LiteralPath $OutputPath

    if ($plusScore -gt $bestScore) {
        $bestScore = $plusScore
        $bestValues = Get-ParameterValues $plusItems
    }
    if ($minusScore -gt $bestScore) {
        $bestScore = $minusScore
        $bestValues = Get-ParameterValues $minusItems
    }

    foreach ($item in $parameters) {
        $gradient = ($plusScore - $minusScore) /
            (2.0 * $item.C * $stepScale * $directions[$item.Name])
        $ak = $item.A / [math]::Pow($iteration + 1, 0.602)
        $theta[$item.Name] = [int][math]::Round(
            $theta[$item.Name] + $ak * $gradient,
            0,
            [MidpointRounding]::AwayFromZero
        )
        if ($theta[$item.Name] -lt $item.Minimum) {
            $theta[$item.Name] = $item.Minimum
        }
        if ($theta[$item.Name] -gt $item.Maximum) {
            $theta[$item.Name] = $item.Maximum
        }
    }

    Write-Host "SPSA update $($theta | Out-String -Width 200)"
}

Write-Host "SPSA baseline score=$baselineScore"
Write-Host "SPSA best screened score=$bestScore"
Write-Host "SPSA best screened parameters: lmrDepth=$($bestValues.lmrDepth) lmrMove=$($bestValues.lmrMove) nullBase=$($bestValues.nullBase) futility=$($bestValues.futility)"
Write-Host "SPSA results: $OutputPath"
