param(
    [Parameter(Mandatory = $true)][string]$ConfigPath,
    [Parameter(Mandatory = $true)][string]$NodePath,
    [Parameter(Mandatory = $true)][string]$LauncherPath
)

$ErrorActionPreference = 'Stop'

function Resolve-AbsoluteFile([string]$Value) {
    $root = [System.IO.Path]::GetPathRoot($Value)
    if (-not [System.IO.Path]::IsPathRooted($Value) -or $root -match '^[A-Za-z]:$|^[\\/]$' -or $Value -match '[\x00-\x1f"]') {
        throw 'Expected an absolute file path without command-line control characters.'
    }
    $item = Get-Item -LiteralPath $Value -ErrorAction Stop
    if ($item.PSProvider.Name -ne 'FileSystem' -or $item.PSIsContainer) { throw 'Expected an existing file.' }
    return $item.FullName
}

function ConvertTo-PowerShellLiteral([string]$Value) {
    if ($Value -match '[\x00-\x1f"]') { throw 'A task argument contains unsupported command-line characters.' }
    return "'" + $Value.Replace("'", "''") + "'"
}

$resolvedConfig = Resolve-AbsoluteFile $ConfigPath
$resolvedNode = Resolve-AbsoluteFile $NodePath
$resolvedLauncher = Resolve-AbsoluteFile $LauncherPath
$supervisor = Resolve-AbsoluteFile (Join-Path $PSScriptRoot 'supervise.ps1')
$powerShell = Resolve-AbsoluteFile (Join-Path ([Environment]::GetFolderPath('System')) 'WindowsPowerShell\v1.0\powershell.exe')
try { $configuration = Get-Content -LiteralPath $resolvedConfig -Raw | ConvertFrom-Json }
catch { throw 'Could not read the private Desktop pilot configuration.' }
if ($configuration.stateDirectory -isnot [string] -or -not [System.IO.Path]::IsPathRooted($configuration.stateDirectory)) {
    throw 'The private pilot state directory must be absolute.'
}
$stateItem = Get-Item -LiteralPath $configuration.stateDirectory -ErrorAction Stop
if ($stateItem.PSProvider.Name -ne 'FileSystem' -or -not $stateItem.PSIsContainer) { throw 'The private pilot state directory is unavailable.' }
$statusFile = Join-Path $stateItem.FullName 'status.json'
$logFile = Join-Path $stateItem.FullName 'launcher.log'
$taskName = 'CodexCiWebhook-DesktopPilot-' + [guid]::NewGuid().ToString('D')
$bootstrapLog = Join-Path $stateItem.FullName ('launcher-bootstrap-{0}.log' -f $taskName)
$quotedArguments = @($supervisor, $resolvedConfig, $resolvedNode, $resolvedLauncher, $taskName, $logFile, $bootstrapLog) | ForEach-Object { ConvertTo-PowerShellLiteral $_ }
$bootstrapCommand = '& {0} -ConfigPath {1} -NodePath {2} -LauncherPath {3} -TaskName {4} -LogPath {5} *> {6}' -f $quotedArguments
$taskArguments = '-NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -Command "{0}"' -f $bootstrapCommand
$identity = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$action = New-ScheduledTaskAction -Execute $powerShell -Argument $taskArguments -WorkingDirectory $PSScriptRoot
$principal = New-ScheduledTaskPrincipal -UserId $identity -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -MultipleInstances IgnoreNew -ExecutionTimeLimit ([TimeSpan]::Zero) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -Hidden
$registered = $false

try {
    # No trigger and no -Force: this is one explicitly requested launch, not an
    # autostart or recurring task, and cannot replace an existing registration.
    Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Settings $settings -Description 'Own one explicitly requested Codex Desktop transport pilot independently of the Desktop process tree.' | Out-Null
    $registered = $true
    Start-ScheduledTask -TaskName $taskName
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $status = $null
        try { $status = Get-Content -LiteralPath $statusFile -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop } catch { }
        if ($null -ne $status -and $status.scheduledTask -ceq $taskName -and $status.launcherPid -is [ValueType]) {
            $launcherProcess = $null
            try {
                $launcherProcess = [System.Diagnostics.Process]::GetProcessById([int]$status.launcherPid)
                if (-not $launcherProcess.HasExited -and [string]::Equals($launcherProcess.MainModule.FileName, $resolvedNode, [StringComparison]::OrdinalIgnoreCase)) {
                    [ordered]@{
                        launcherPid = [int]$status.launcherPid
                        statusFile = $statusFile
                        taskName = $taskName
                        waitingForIdle = ($status.phase -ceq 'waiting-for-idle')
                    } | ConvertTo-Json -Compress
                    return
                }
            } catch { }
            finally { if ($null -ne $launcherProcess) { $launcherProcess.Dispose() } }
        }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    # Capture the task service's result before the failure path removes an inert
    # registration. These fields contain lifecycle metadata, never configuration
    # contents, tokens, command lines or conversation messages.
    $launchFailure = [ordered]@{
        TaskName = $taskName
        State = $null
        LastTaskResult = $null
        LastRunTime = $null
        BootstrapLog = $bootstrapLog
    }
    try { $launchFailure.State = [string](Get-ScheduledTask -TaskName $taskName -ErrorAction Stop).State } catch { }
    try {
        $taskInfo = Get-ScheduledTaskInfo -TaskName $taskName -ErrorAction Stop
        $launchFailure.LastTaskResult = $taskInfo.LastTaskResult
        $launchFailure.LastRunTime = $taskInfo.LastRunTime.ToUniversalTime().ToString('o')
    } catch { }
    try {
        [System.IO.File]::WriteAllText((Join-Path $stateItem.FullName 'task-launch-error.json'), ($launchFailure | ConvertTo-Json), [System.Text.UTF8Encoding]::new($false))
    } catch { }
    throw 'The independent Desktop pilot did not acknowledge startup within 15 seconds.'
} catch {
    if ($registered) {
        try {
            $ownTask = Get-ScheduledTask -TaskName $taskName -ErrorAction Stop
            # A running or queued task may already own a live launcher. Leave it
            # registered so error handling cannot terminate the independent owner.
            if ([string]$ownTask.State -notin @('Running', 'Queued') -and @($ownTask.Actions).Count -eq 1 -and $ownTask.Actions[0].Arguments -ceq $taskArguments -and [string]::Equals($ownTask.Actions[0].Execute, $powerShell, [StringComparison]::OrdinalIgnoreCase)) {
                Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction Stop
            }
        } catch { }
    }
    throw
}
