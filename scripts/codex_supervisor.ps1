param(
    [string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$Model = "gpt-5.3-codex",
    [string]$ReasoningEffort = "xhigh",
    [string]$Sandbox = "danger-full-access",
    [int]$SleepSeconds = 10,
    [int]$MaxRuns = 0,
    [switch]$Fresh,
    [switch]$Search,
    [switch]$StopOnBlocker,
    [switch]$SkipGitRepoCheck
)

$scriptPath = Join-Path $PSScriptRoot "codex_supervisor.js"
$cmd = @(
    $scriptPath,
    "--repo", $Repo,
    "--model", $Model,
    "--reasoning-effort", $ReasoningEffort,
    "--sandbox", $Sandbox,
    "--sleep-seconds", $SleepSeconds.ToString(),
    "--max-runs", $MaxRuns.ToString()
)

if ($Fresh) {
    $cmd += "--fresh"
}

if ($Search) {
    $cmd += "--search"
}

if ($StopOnBlocker) {
    $cmd += "--stop-on-blocker"
}

if ($SkipGitRepoCheck) {
    $cmd += "--skip-git-repo-check"
}

& node @cmd
exit $LASTEXITCODE
