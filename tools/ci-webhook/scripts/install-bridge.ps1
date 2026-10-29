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
if (-not (Test-Path -LiteralPath $bridgeConfig.codexPath -PathType Leaf)) { throw 'Configured Codex executable does not exist.' }
$probePath = Join-Path $sourceRoot 'bridge/app-server.mjs'
& $nodePath $probePath --probe $bridgeConfig.appServer
if ($LASTEXITCODE -ne 0) { throw 'Configured Codex app-server is unavailable. Start the shared Desktop endpoint first; delivery remains unverified.' }
if (-not (Test-Path -LiteralPath $bridgeConfig.tokenFile)) { throw 'Bridge token file does not exist.' }
$health = Invoke-RestMethod -Uri ($bridgeConfig.endpoint.TrimEnd('/') + '/health') -TimeoutSec 15
if ($health.service -ne 'codex-ci-webhook') { throw 'Receiver health check failed.' }
$identity = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$action = New-ScheduledTaskAction -Execute $powershellPath -Argument ('-NoProfile -NonInteractive -WindowStyle Hidden -File "{0}" -ConfigPath "{1}" -NodePath "{2}"' -f $launcherPath, $resolvedConfig, $nodePath)
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $identity
$principal = New-ScheduledTaskPrincipal -UserId $identity -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -MultipleInstances IgnoreNew -StartWhenAvailable -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Description 'Receive GitHub CI webhooks over an outbound WebSocket and queue results in Codex.' -Force | Out-Null
Start-ScheduledTask -TaskName $TaskName
Write-Output "Installed and started: $TaskName"
