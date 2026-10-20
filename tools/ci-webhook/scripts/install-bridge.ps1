param(
    [Parameter(Mandatory = $true)][string]$ConfigPath,
    [string]$TaskName = 'Codex CI webhook bridge'
)
$ErrorActionPreference = 'Stop'
$resolvedConfig = (Resolve-Path -LiteralPath $ConfigPath).Path
$bridgeConfig = Get-Content -LiteralPath $resolvedConfig -Raw | ConvertFrom-Json
$sourceRoot = Split-Path -Parent $PSScriptRoot
$nodePath = (Get-Command node.exe).Source
$launcherPath = Join-Path $PSScriptRoot 'start-bridge.ps1'
$powershellPath = (Get-Command powershell.exe).Source
if ($bridgeConfig.appServer -notlike 'unix://*') { throw 'An existing local Codex control socket is required.' }
$socketPath = $bridgeConfig.appServer.Substring(7)
if (-not $socketPath) {
    $codexBase = if ($env:CODEX_HOME) { $env:CODEX_HOME } else { Join-Path $env:USERPROFILE '.codex' }
    $socketPath = Join-Path $codexBase 'app-server-control/app-server-control.sock'
}
if (-not (Test-Path -LiteralPath $socketPath)) { throw 'Codex Desktop has no shared control socket. Do not start a second server for its active conversation.' }
if (-not (Test-Path -LiteralPath $bridgeConfig.tokenFile)) { throw 'Bridge token file does not exist.' }
$health = Invoke-RestMethod -Uri ($bridgeConfig.endpoint.TrimEnd('/') + '/health') -TimeoutSec 15
if ($health.service -ne 'codex-ci-webhook') { throw 'Receiver health check failed.' }
$identity = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$action = New-ScheduledTaskAction -Execute $powershellPath -Argument ('-NoProfile -NonInteractive -WindowStyle Hidden -File "{0}" -ConfigPath "{1}" -NodePath "{2}"' -f $launcherPath, $resolvedConfig, $nodePath)
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $identity
$principal = New-ScheduledTaskPrincipal -UserId $identity -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -MultipleInstances IgnoreNew -StartWhenAvailable -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)
Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Description 'Receive GitHub CI webhooks over an outbound WebSocket and queue results in Codex.' -Force | Out-Null
Start-ScheduledTask -TaskName $TaskName
Write-Output "Installed and started: $TaskName"
