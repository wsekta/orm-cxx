param(
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [ValidateSet('register', 'status', 'list', 'close', 'transfer', 'adopt', 'reconcile', 'drain')][string]$Command = 'status',
    [string[]]$Arguments = @()
)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.schemaVersion -ne 2) { throw 'Unsupported installed webhook manifest.' }
foreach ($property in @('runtimePath', 'nodePath', 'configPath')) {
    $value = $manifest.$property
    if (-not [IO.Path]::IsPathRooted($value) -or -not (Test-Path -LiteralPath $value)) {
        throw ('Installed manifest has an invalid {0}.' -f $property)
    }
}
if ($Arguments -contains '--config') { throw 'Use configPath from the installed manifest.' }
$entry = switch ($Command) {
    'register' { 'bridge/register.mjs' }
    'drain' { 'bridge/drain.mjs' }
    default { 'bridge/manage.mjs' }
}
$scriptPath = Join-Path $manifest.runtimePath $entry
if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) { throw 'Installed runtime does not contain this command.' }
$commandArguments = @($scriptPath)
if ($Command -notin @('register', 'drain')) { $commandArguments += $Command }
$commandArguments += @('--config', $manifest.configPath)
$commandArguments += $Arguments
# Windows PowerShell 5.1 must not turn Node's SQLite stderr warning into a
# terminating error when the command itself succeeds.
$previousPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    & $manifest.nodePath @commandArguments
    $commandExitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $previousPreference
}
if ($commandExitCode -ne 0) { throw ('Webhook {0} failed with exit code {1}.' -f $Command, $commandExitCode) }
