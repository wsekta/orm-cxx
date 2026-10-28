param(
    [Parameter(Mandatory = $true)][string]$ConfigPath,
    [Parameter(Mandatory = $true)][string]$NodePath
)
$ErrorActionPreference = 'Stop'
$bridgeConfig = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
$logRoot = Join-Path (Split-Path -Parent $bridgeConfig.journalPath) 'logs'
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$entryPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'bridge/main.mjs'
$logPath = Join-Path $logRoot 'bridge.log'
if ((Test-Path -LiteralPath $logPath) -and (Get-Item -LiteralPath $logPath).Length -gt 5MB) {
    Move-Item -LiteralPath $logPath -Destination ($logPath + '.previous') -Force
}
# Native stderr belongs in the private log; a warning must not terminate the
# foreground wrapper and its owned Node process on Windows PowerShell 5.1.
$ErrorActionPreference = 'Continue'
& $NodePath $entryPath --config $ConfigPath *> $logPath
$bridgeExitCode = $LASTEXITCODE
exit $bridgeExitCode
