<#
.SYNOPSIS
    Orchestrates the deterministic plumbing of an upstream merge pass (libultraship / soh / mm).

.DESCRIPTION
    Reads upstream-pins.json (last-merged SHA per vendored folder), then for each upstream:
      1. ensures the remote exists + fetches the tracked branch,
      2. hydrates blobs via the forced filter-disabled refetch (HarbourMasters servers refuse
         by-SHA promisor fetches; a plain fetch is a no-op for promisor-absent blobs),
      3. rebuilds the vendor-<name> branch at the current tip (prefixed tree, parent = prior
         vendor tip) using the grafted-ancestry mechanism,
      4. prints the CONFLICT SURFACE report: which of OUR locally-customized files upstream
         touched since the last-merged SHA (the real work-list for the merge).

    With -Merge it also runs the 3-way merges (renames disabled for mm) on the current branch,
    leaving any conflicts for a human to resolve. It NEVER commits.

    The semantic conflict resolution + build-fix chain is intentionally NOT automated.
    See docs/UPSTREAM_MERGES.md.

.PARAMETER Merge
    After reporting, run `git merge vendor-<name>` for each upstream (order: libultraship, soh, mm).

.PARAMETER Only
    Restrict to one upstream key: libultraship | soh | mm.

.PARAMETER Depth
    Refetch depth for blob hydration (default 50). Increase if the fork is far behind the tip.

.EXAMPLE
    pwsh scripts/upstream-merge.ps1                 # fetch + rebuild vendor branches + report
    pwsh scripts/upstream-merge.ps1 -Merge          # ...then run the 3-way merges
    pwsh scripts/upstream-merge.ps1 -Only soh -Depth 600
#>
[CmdletBinding()]
param(
    [switch]$Merge,
    [ValidateSet('libultraship', 'soh', 'mm')]
    [string]$Only,
    [int]$Depth = 50
)

$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..')
Set-Location $repo

# Auto-gc collides with promisor fetches ("Permission denied writing pack").
git config gc.auto 0 | Out-Null

$remotes = @{ libultraship = 'up-lus'; soh = 'up-soh'; mm = 'up-mm' }

$pins = Get-Content (Join-Path $repo 'upstream-pins.json') -Raw | ConvertFrom-Json
$keys = if ($Only) { @($Only) } else { @('libultraship', 'soh', 'mm') }

foreach ($key in $keys) {
    $u = $pins.upstreams.$key
    $remote = $remotes[$key]
    $branch = $u.branch
    $prefix = $u.prefix
    $subtree = $u.subtree   # '' means the upstream repo root maps to our prefix
    $vendorBranch = "vendor-$key"

    Write-Host "`n==================== $key ($remote/$branch -> $prefix/) ====================" -ForegroundColor Cyan

    # 1. Ensure remote + fetch (blob:none promisor).
    if (-not (git remote | Select-String -SimpleMatch $remote)) {
        Write-Host "  adding remote $remote -> $($u.remote)" -ForegroundColor Yellow
        git remote add --no-tags -t $branch $remote $u.remote
        git config "remote.$remote.promisor" true
        git config "remote.$remote.partialclonefilter" 'blob:none'
    }
    git fetch $remote $branch 2>&1 | Select-Object -Last 2 | ForEach-Object { "  $_" }

    $tip = (git rev-parse "$remote/$branch").Trim()
    $merged = $u.mergedSha
    Write-Host "  last-merged $merged  ->  tip $($tip.Substring(0,9))"

    if ((git rev-parse $merged) -eq (git rev-parse $tip)) {
        Write-Host "  up to date, nothing new." -ForegroundColor Green
        continue
    }

    # 2. Hydrate blobs (forced, filter-disabled refetch).
    Write-Host "  hydrating blobs (refetch depth=$Depth)..." -ForegroundColor Yellow
    git -c "remote.$remote.promisor=false" -c "remote.$remote.partialclonefilter=" fetch --refetch --depth=$Depth $remote $branch 2>&1 | Select-Object -Last 1 | ForEach-Object { "  $_" }

    # 3. Rebuild vendor-<name> at tip (prefixed tree, parent = prior vendor tip).
    $tipTreeRef = if ($subtree) { "${tip}:$subtree" } else { "${tip}^{tree}" }
    $sub = (git rev-parse $tipTreeRef).Trim()
    $newTree = ("040000 tree $sub`t$prefix" | git mktree).Trim()
    $newCommit = (git commit-tree $newTree -p $vendorBranch -m "vendor: $key $branch $($tip.Substring(0,9))").Trim()
    git branch -f $vendorBranch $newCommit | Out-Null
    Write-Host "  rebuilt $vendorBranch @ $($newCommit.Substring(0,9))"

    # 4. Conflict-surface report: our locally-modified files INTERSECT upstream-changed files.
    $ours = git diff --name-only "$vendorBranch~1" HEAD -- $prefix | Sort-Object
    $upstreamChanged = git diff --name-only $merged $tip | ForEach-Object {
        if ($subtree) { $_ } else { "$prefix/$_" }
    } | Sort-Object
    $surface = $ours | Where-Object { $upstreamChanged -contains $_ }

    Write-Host "  --- CONFLICT SURFACE (our customized files upstream touched) ---" -ForegroundColor Magenta
    if ($surface) { $surface | ForEach-Object { Write-Host "    $_" } }
    else { Write-Host "    (none — should auto-merge clean)" -ForegroundColor Green }
    Write-Host "    [$($surface.Count) of $($ours.Count) customized files; upstream changed $($upstreamChanged.Count)]"
}

# Optional: run the merges (order matters: libultraship -> soh -> mm).
if ($Merge) {
    Write-Host "`n==================== running 3-way merges ====================" -ForegroundColor Cyan
    foreach ($key in @('libultraship', 'soh', 'mm') | Where-Object { -not $Only -or $_ -eq $Only }) {
        $vb = "vendor-$key"
        Write-Host "`n  git merge $vb" -ForegroundColor Yellow
        if ($key -eq 'mm') {
            git -c merge.renames=false merge --no-ff --no-commit $vb 2>&1 | ForEach-Object { "    $_" }
        } else {
            git merge --no-ff --no-commit $vb 2>&1 | ForEach-Object { "    $_" }
        }
        $conf = git diff --name-only --diff-filter=U
        if ($conf) {
            Write-Host "  CONFLICTS to resolve by hand:" -ForegroundColor Magenta
            $conf | ForEach-Object { Write-Host "    $_" }
            Write-Host "  Resolve, stage, then `git commit`. mm asset modify/deletes: `git checkout $vb -- mm/assets`." -ForegroundColor Yellow
            break  # stop so the human resolves before the next folder
        } else {
            Write-Host "  clean — review staged changes, then commit." -ForegroundColor Green
        }
    }
}

Write-Host "`nDone. After committing all merges: (1) add docs/merges/<YYYY-MM-DD>.md with the per-folder" -ForegroundColor Cyan
Write-Host "post-merge changes and link it from docs/UPSTREAM_MERGES.md, (2) update upstream-pins.json mergedSha/mergedDate." -ForegroundColor Cyan
