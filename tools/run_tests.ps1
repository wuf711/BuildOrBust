# Build or Bust - headless self-test runner.
#
#   powershell -File tools\run_tests.ps1              # all scenarios
#   powershell -File tools\run_tests.ps1 -Scenario boss
#
# Launches the game headless, runs BoB.Test, waits for the VERDICT line,
# then prints only PASS/FAIL rows and exits non-zero if anything broke.
# Exit code is what CI (or you) should look at - no need to read the log.
#
# NOTE: keep this file ASCII-only. PowerShell 5.1 reads BOM-less .ps1 as ANSI,
# so CJK in here comes out as garbage on this machine.

param(
    [string]$Scenario = "all",
    [int]$TimeoutSec = 420
)

$ErrorActionPreference = "Stop"

$UE  = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$Proj = (Resolve-Path (Join-Path $PSScriptRoot "..\BuildOrBust.uproject")).Path
$Root = Split-Path -Parent $Proj
$Map  = "/Game/Variant_Shooter/Lvl_Shooter"
$Log  = Join-Path $Root "Saved\Logs\bobtest.log"

if (Test-Path $Log) { Remove-Item $Log -Force }

# -forcelogflush is required: we kill the process at the end, and without it
# the tail of the log (which holds the verdict) is lost in the buffer.
$ueArgs = @(
    $Proj, $Map,
    "-game", "-unattended", "-nullrhi", "-nosound", "-nosplash", "-forcelogflush",
    "-ExecCmds=`"BoB.AutoReady 1, BoB.Test $Scenario`"",
    "-log", "ABSLOG=$Log"
)

Write-Host "[run_tests] scenario='$Scenario'  (cold map load takes ~150s, be patient)"
$p = Start-Process -FilePath $UE -ArgumentList $ueArgs -PassThru -WindowStyle Hidden

$deadline = (Get-Date).AddSeconds($TimeoutSec)
$verdict = $null
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 5
    if (Test-Path $Log) {
        $hit = Select-String -Path $Log -Pattern "\[BoBTest\] VERDICT" -ErrorAction SilentlyContinue
        if ($hit) { $verdict = $hit[-1].Line; break }
    }
    if ($p.HasExited) { break }
}

if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Get-Process UnrealEditor-Cmd -ErrorAction SilentlyContinue | Stop-Process -Force

if (-not (Test-Path $Log)) {
    Write-Host "[run_tests] no log produced - did the editor fail to launch?"
    exit 2
}

Write-Host ""
Select-String -Path $Log -Pattern "\[BoBTest\]" | ForEach-Object {
    ($_.Line -replace '^\[[0-9.:-]+\]\[[ 0-9]+\]LogTemp: ', '')
}
Write-Host ""

if (-not $verdict) {
    Write-Host "[run_tests] TIMEOUT - no verdict after $TimeoutSec s"
    exit 3
}
if ($verdict -match "VERDICT OK") {
    Write-Host "[run_tests] OK"
    exit 0
}
Write-Host "[run_tests] BROKEN"
exit 1
