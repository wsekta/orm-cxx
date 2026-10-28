param(
    [Parameter(Mandatory = $true)][string]$BridgeConfig,
    [Parameter(Mandatory = $true)][string]$Thread,
    [Parameter(Mandatory = $true)][string]$PythonPath,
    [Parameter(Mandatory = $true)][string]$NodePath,
    [Parameter(Mandatory = $true)][string]$StateDirectory,
    [string]$AppServer = 'ws://127.0.0.1:49175',
    [string[]]$IgnoreBusyTurns = @()
)
$ErrorActionPreference = 'Stop'
$nativeHelper = Join-Path $PSScriptRoot 'windows.py'
$package = Get-AppxPackage 'OpenAI.Codex'
if (@($package).Count -ne 1) { throw 'Expected one installed Desktop package.' }
$desktopPath = Join-Path $package.InstallLocation 'app/ChatGPT.exe'
$desktopProcesses = @(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -eq $desktopPath })
$servers = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -eq 'codex.exe' -and $_.CommandLine -match '\bapp-server\b' -and
    $desktopProcesses.ProcessId -contains $_.ParentProcessId
})
if ($servers.Count -ne 1) { throw 'Expected one app-server owned by this Desktop package.' }
$server = $servers[0]
$desktopProcess = $desktopProcesses | Where-Object { $_.ProcessId -eq $server.ParentProcessId }
if (-not $desktopProcess) { throw 'Desktop parent process was not identified.' }
$captured = @{ commandLine = $server.CommandLine } | ConvertTo-Json -Compress | & $PythonPath $nativeHelper arguments
if ($LASTEXITCODE -ne 0) { throw 'Could not parse the captured app-server arguments.' }
$serverArguments = @((($captured | ConvertFrom-Json).arguments) | Select-Object -Skip 1)
$desktopIdentity = & $PythonPath $nativeHelper shutdown-plan --pid $desktopProcess.ProcessId
if ($LASTEXITCODE -ne 0) { throw 'Desktop shutdown preflight failed; no restart was attempted.' }
$serverIdentity = & $PythonPath $nativeHelper shutdown-plan --pid $server.ProcessId
if ($LASTEXITCODE -ne 0) { throw 'App-server identity preflight failed; no restart was attempted.' }
$codexStateHome = if ($env:CODEX_HOME) { $env:CODEX_HOME } else { Join-Path $env:USERPROFILE '.codex' }
$sqliteStateHome = if ($env:CODEX_SQLITE_HOME) { $env:CODEX_SQLITE_HOME } else { $codexStateHome }
# A separate sqlite_home in config must be made explicit, rather than silently
# snapshotting the wrong store. The normal installation currently uses one home.
$configPath = Join-Path $codexStateHome 'config.toml'
if ((Test-Path -LiteralPath $configPath) -and (Select-String -LiteralPath $configPath -Pattern '^\s*sqlite_home\s*=' -Quiet)) { throw 'Resolve configured sqlite_home explicitly before preparing this pilot.' }
$resolvedState = [System.IO.Path]::GetFullPath($StateDirectory)
New-Item -ItemType Directory -Path $resolvedState -Force | Out-Null
$identity = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
& icacls.exe $resolvedState /inheritance:r /grant:r ('{0}:(OI)(CI)F' -f $identity) /grant:r 'SYSTEM:(OI)(CI)F' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Could not protect the private pilot state directory.' }
try { $bridge = Get-Content -LiteralPath $BridgeConfig -Raw | ConvertFrom-Json }
catch { throw 'Could not read the existing private bridge configuration.' }
# Get-Item/Resolve-Path can retain a packaged app's virtual AppData view. Resolve
# file handles through Python so the independent task sees the identical files.
$pathInputs = [ordered]@{
    desktopPath = $desktopPath
    runtimeRoot = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'OpenAI/Codex/bin'
    codexHome = $codexStateHome
    sqliteHome = $sqliteStateHome
    pythonPath = (Resolve-Path -LiteralPath $PythonPath).Path
    nodePath = (Resolve-Path -LiteralPath $NodePath).Path
    bridgeConfig = (Resolve-Path -LiteralPath $BridgeConfig).Path
    stateDirectory = $resolvedState
    bridgeToken = $bridge.tokenFile
    bridgeJournal = $bridge.journalPath
}
$physicalOutput = $pathInputs | ConvertTo-Json -Compress | & $PythonPath $nativeHelper paths
if ($LASTEXITCODE -ne 0) { throw 'Could not resolve physical paths for the independent supervisor.' }
$physical = $physicalOutput | ConvertFrom-Json
$configuration = [ordered]@{
    desktopPath = $physical.desktopPath
    runtimeRoot = $physical.runtimeRoot
    codexHome = $physical.codexHome
    sqliteHome = $physical.sqliteHome
    pythonPath = $physical.pythonPath
    nodePath = $physical.nodePath
    bridgeConfig = $physical.bridgeConfig
    stateDirectory = $physical.stateDirectory
    bridgePaths = [ordered]@{ tokenFile = $physical.bridgeToken; journalPath = $physical.bridgeJournal }
    thread = $Thread
    appServer = $AppServer
    desktopPid = [int]$desktopProcess.ProcessId
    desktopCreated = ($desktopIdentity | ConvertFrom-Json).requestedCreationTime
    serverPid = [int]$server.ProcessId
    serverCreated = ($serverIdentity | ConvertFrom-Json).requestedCreationTime
    serverArguments = $serverArguments
    ignoreBusyTurns = @($IgnoreBusyTurns)
}
$target = Join-Path $physical.stateDirectory 'desktop.local.json'
[System.IO.File]::WriteAllText($target, ($configuration | ConvertTo-Json -Depth 5), [System.Text.UTF8Encoding]::new($false))
Write-Output $target
