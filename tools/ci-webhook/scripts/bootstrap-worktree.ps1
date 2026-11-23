param(
    [Parameter(Mandatory = $true)][string]$Worktree,
    [Parameter(Mandatory = $true)][string]$SourceWorktree,
    [Parameter(Mandatory = $true)][string]$ManifestPath
)
$ErrorActionPreference = 'Stop'
function Read-Git([string]$Root, [string[]]$GitArguments) {
    $result = & git -C $Root @GitArguments
    if ($LASTEXITCODE -ne 0) { throw 'Unable to verify worktree identity.' }
    return ($result | Out-String).Trim()
}
$source = (Resolve-Path -LiteralPath $SourceWorktree).Path
$target = (Resolve-Path -LiteralPath $Worktree).Path
$sourceCommon = Read-Git $source @('rev-parse', '--path-format=absolute', '--git-common-dir')
$targetCommon = Read-Git $target @('rev-parse', '--path-format=absolute', '--git-common-dir')
if (-not [string]::Equals($sourceCommon, $targetCommon, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Source and target must be worktrees of the same repository.'
}
$targetRoot = Read-Git $target @('rev-parse', '--show-toplevel')
if (-not [string]::Equals([IO.Path]::GetFullPath($targetRoot), [IO.Path]::GetFullPath($target), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Worktree must name the worktree root.'
}
$known = @(Read-Git $source @('worktree', 'list', '--porcelain')) -split "`r?`n" |
    Where-Object { $_.StartsWith('worktree ') } |
    ForEach-Object { [IO.Path]::GetFullPath($_.Substring(9)) }
if (-not ($known | Where-Object { [string]::Equals($_, $target, [StringComparison]::OrdinalIgnoreCase) })) {
    throw 'Target is not a registered worktree.'
}
$resolvedManifest = (Resolve-Path -LiteralPath $ManifestPath).Path
$manifest = Get-Content -LiteralPath $resolvedManifest -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.schemaVersion -ne 2) { throw 'Unsupported installed webhook manifest.' }
$runtimeReadme = Join-Path $manifest.runtimePath 'README.md'
$entry = Join-Path $manifest.runtimePath 'scripts/webhook.ps1'
if (-not (Test-Path -LiteralPath $entry -PathType Leaf) -or -not (Test-Path -LiteralPath $runtimeReadme -PathType Leaf)) {
    throw 'Installed runtime is incomplete.'
}
$sourceInstructions = Join-Path $source 'AGENTS.md'
$targetInstructions = Join-Path $target 'AGENTS.md'
if (-not (Test-Path -LiteralPath $sourceInstructions -PathType Leaf)) { throw 'Source worktree has no local AGENTS.md.' }
if (Test-Path -LiteralPath $targetInstructions) {
    if ((Get-FileHash -LiteralPath $sourceInstructions).Hash -ne (Get-FileHash -LiteralPath $targetInstructions).Hash) {
        throw 'Target AGENTS.md already differs; preserve it and reconcile instructions explicitly.'
    }
} else {
    Copy-Item -LiteralPath $sourceInstructions -Destination $targetInstructions
}
$localDirectory = Join-Path $target '.scratch'
New-Item -ItemType Directory -Path $localDirectory -Force | Out-Null
$pointerPath = Join-Path $localDirectory 'ci-webhook-runtime.md'
$pointer = @"
# Installed webhook runtime

For every push from this worktree, register its exact repository, branch, full
HEAD and this conversation's UUID with the installed CLI before pushing.
Use separate worktrees for parallel features.

Installed manifest: $resolvedManifest
Stable entrypoint: $entry
Runtime procedure: $runtimeReadme

Use the register command with --worktree ABSOLUTE_WORKTREE --branch BRANCH
--sha FULL_SHA --thread CURRENT_CONVERSATION_UUID. A successful registration is
not proof of Desktop delivery; verify the same idle conversation separately.
After pushing, record the SHA and end the turn for webhook resumption.
"@
if (Test-Path -LiteralPath $pointerPath) {
    $existing = Get-Content -LiteralPath $pointerPath -Raw -Encoding UTF8
    if ($existing.TrimEnd() -ne $pointer.TrimEnd()) {
        throw 'Target runtime pointer already differs; preserve it and reconcile explicitly.'
    }
} else {
    [IO.File]::WriteAllText($pointerPath, $pointer, [Text.UTF8Encoding]::new($false))
}
Write-Output 'Worktree instructions and runtime pointer are ready; no credentials or journal were copied.'
