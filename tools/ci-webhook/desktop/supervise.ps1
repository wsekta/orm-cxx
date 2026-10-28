param(
    [Parameter(Mandatory = $true)][string]$ConfigPath,
    [Parameter(Mandatory = $true)][string]$NodePath,
    [Parameter(Mandatory = $true)][string]$LauncherPath,
    [Parameter(Mandatory = $true)][string]$TaskName,
    [Parameter(Mandatory = $true)][string]$LogPath
)

$ErrorActionPreference = 'Stop'
$exitCode = 1
$ownsTask = $false

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

try {
    if ($TaskName -cnotmatch '^CodexCiWebhook-DesktopPilot-[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$') { throw 'Invalid pilot task identity.' }
    $resolvedConfig = Resolve-AbsoluteFile $ConfigPath
    $resolvedNode = Resolve-AbsoluteFile $NodePath
    $resolvedLauncher = Resolve-AbsoluteFile $LauncherPath
    $supervisor = Resolve-AbsoluteFile $PSCommandPath
    $powerShell = Resolve-AbsoluteFile (Join-Path ([Environment]::GetFolderPath('System')) 'WindowsPowerShell\v1.0\powershell.exe')
    try { $configuration = Get-Content -LiteralPath $resolvedConfig -Raw | ConvertFrom-Json }
    catch { throw 'Could not read the private Desktop pilot configuration.' }
    if ($configuration.stateDirectory -isnot [string] -or -not [System.IO.Path]::IsPathRooted($configuration.stateDirectory)) { throw 'The private pilot state directory must be absolute.' }
    $stateItem = Get-Item -LiteralPath $configuration.stateDirectory -ErrorAction Stop
    if ($stateItem.PSProvider.Name -ne 'FileSystem' -or -not $stateItem.PSIsContainer) { throw 'The private pilot state directory is unavailable.' }
    $expectedLogPath = Join-Path $stateItem.FullName 'launcher.log'
    if (-not [string]::Equals($LogPath, $expectedLogPath, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected private launcher log path.' }
    # Establish the private log before querying the task service or comparing
    # its returned action. A rejected invocation must remain diagnosable too.
    $bootstrapLog = Join-Path $stateItem.FullName ('launcher-bootstrap-{0}.log' -f $TaskName)
    $quotedArguments = @($supervisor, $resolvedConfig, $resolvedNode, $resolvedLauncher, $TaskName, $LogPath, $bootstrapLog) | ForEach-Object { ConvertTo-PowerShellLiteral $_ }
    $bootstrapCommand = '& {0} -ConfigPath {1} -NodePath {2} -LauncherPath {3} -TaskName {4} -LogPath {5} *> {6}' -f $quotedArguments
    $expectedArguments = '-NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -Command "{0}"' -f $bootstrapCommand
    $ownTask = Get-ScheduledTask -TaskName $TaskName -ErrorAction Stop
    $actionCount = @($ownTask.Actions).Count
    $argumentsMatch = $actionCount -eq 1 -and $ownTask.Actions[0].Arguments -ceq $expectedArguments
    $argumentsMatchIgnoringCase = $actionCount -eq 1 -and $ownTask.Actions[0].Arguments -ieq $expectedArguments
    $executableMatches = $actionCount -eq 1 -and [string]::Equals($ownTask.Actions[0].Execute, $powerShell, [StringComparison]::OrdinalIgnoreCase)
    if (-not $argumentsMatch -or -not $executableMatches) {
        throw ('The scheduled task does not belong to this supervisor invocation (actions={0}, argumentsMatch={1}, argumentsMatchIgnoringCase={2}, executableMatches={3}).' -f $actionCount, $argumentsMatch, $argumentsMatchIgnoringCase, $executableMatches)
    }
    $ownsTask = $true
    $nodeArguments = @($resolvedLauncher, '--config', $resolvedConfig, '--switch', '--scheduled-task', $TaskName)
    # Keep this task action alive for the entire Node supervisor lifetime. There
    # is no detached child, job breakaway attempt, automatic retry or UI window.
    # Continue lets native stderr reach the log on Windows PowerShell 5.1 rather
    # than treating one stderr line as a terminating PowerShell exception.
    $ErrorActionPreference = 'Continue'
    & $resolvedNode @nodeArguments 2>&1 | Out-File -FilePath $logPath -Append -Encoding utf8
    $nativeExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($null -ne $nativeExit) { $exitCode = [int]$nativeExit }
} catch {
    $ErrorActionPreference = 'Continue'
    if ($null -ne $logPath) {
        ('[{0}] Desktop pilot supervisor failed: {1}' -f [DateTime]::UtcNow.ToString('o'), $_.Exception.Message) | Out-File -FilePath $logPath -Append -Encoding utf8
    }
    $exitCode = 1
} finally {
    if ($ownsTask) {
        try {
            $ownTask = Get-ScheduledTask -TaskName $TaskName -ErrorAction Stop
            if (@($ownTask.Actions).Count -eq 1 -and $ownTask.Actions[0].Arguments -ceq $expectedArguments -and [string]::Equals($ownTask.Actions[0].Execute, $powerShell, [StringComparison]::OrdinalIgnoreCase)) {
                # Node has returned. Removing the registration does not request
                # Stop-ScheduledTask or stop any child process.
                Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction Stop
            }
        } catch { }
    }
}

exit $exitCode
