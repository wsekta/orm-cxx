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
& $NodePath $entryPath --config $ConfigPath *> $logPath
exit $LASTEXITCODE
