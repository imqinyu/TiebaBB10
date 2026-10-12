[CmdletBinding(SupportsShouldProcess = $true)]
param([ValidateSet('clean', 'distclean')][string]$Mode = 'clean')
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$variants = @('arm\o.le-v7-g', 'arm\o.le-v7', 'arm-p\o.le-v7-g', 'x86\o-g')
foreach ($variant in $variants) {
    $directory = [IO.Path]::GetFullPath((Join-Path $projectRoot $variant))
    if (-not $directory.StartsWith($projectRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Build cleanup outside the project is forbidden.'
    }
    if (-not (Test-Path -LiteralPath $directory)) { continue }
    # Do not follow junctions in either the architecture or variant directory.
    foreach ($checkPath in @((Split-Path -Parent $directory), $directory)) {
        if ((Get-Item -LiteralPath $checkPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Build cleanup of linked directories is forbidden.'
        }
    }
    $targets = if ($Mode -eq 'distclean') { @($directory) } else {
        @('.obj', '.moc', '.rcc', 'BBTiebaUI', 'BBTiebaUI.so', 'BBTiebaUI.so.sym',
            'libBBTiebaUI.so', 'libBBTiebaUI.so.1', 'libBBTiebaUI.so.1.0', 'libBBTiebaUI.so.1.0.0') |
            ForEach-Object { Join-Path $directory $_ }
    }
    foreach ($target in $targets) {
        $absoluteTarget = [IO.Path]::GetFullPath($target)
        if (-not $absoluteTarget.StartsWith($projectRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Unsafe build cleanup target.'
        }
        if (Test-Path -LiteralPath $absoluteTarget) {
            if ((Get-Item -LiteralPath $absoluteTarget -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw 'Build cleanup of a linked target is forbidden.'
            }
            if ($PSCmdlet.ShouldProcess($absoluteTarget, 'Remove generated build output')) {
                Remove-Item -LiteralPath $absoluteTarget -Recurse -Force
            }
        }
    }
}
