param(
    [string]$Port = "COM4",
    [int]$Baud = 115200,
    [double]$Timeout = 2.0,
    [int]$PingCount = 80,
    [switch]$SkipLed
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$venvPython = Join-Path $scriptDir ".venv\Scripts\python.exe"

if (Test-Path $venvPython) {
    $pythonExe = $venvPython
} else {
    $pythonExe = "python"
}

$scriptPath = Join-Path $scriptDir "nualink_regression.py"
$argsList = @(
    $scriptPath,
    $Port,
    "--baud", $Baud,
    "--timeout", $Timeout,
    "--ping-count", $PingCount
)

if ($SkipLed.IsPresent) {
    $argsList += "--skip-led"
}

& $pythonExe @argsList
exit $LASTEXITCODE
