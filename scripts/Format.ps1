[CmdletBinding()]
param(
    [string]$Path
)

$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ignoredDirectories = @(
    '.git',
    '.vs',
    '.idea',
    '.covdbg',
    'build',
    'out',
    'cmake-build-debug',
    'cmake-build-release',
    'cmake-build-relwithdebinfo',
    'cmake-build-minsizerel',
    'vcpkg',
    'vcpkg_installed'
)
$allowedExtensions = @('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx')

function Resolve-ClangFormat {
    $command = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        'C:\Program Files\LLVM\bin\clang-format.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\bin\clang-format.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\ARM64\bin\clang-format.exe'
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }

    throw 'clang-format executable was not found. Install LLVM or add clang-format to PATH.'
}

function Get-RelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$AbsolutePath
    )

    $fullPath = [System.IO.Path]::GetFullPath($AbsolutePath)
    $relative = [System.IO.Path]::GetRelativePath($repoRoot, $fullPath)
    return $relative.Replace('\', '/')
}

function Test-IsIgnoredPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CandidatePath
    )

    $relativePath = Get-RelativePath -AbsolutePath $CandidatePath
    $segments = $relativePath.Split('/', [System.StringSplitOptions]::RemoveEmptyEntries)

    foreach ($segment in $segments) {
        if ($ignoredDirectories -contains $segment) {
            return $true
        }
    }

    return $false
}

function Test-IsFormattableFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CandidatePath
    )

    if (-not (Test-Path -LiteralPath $CandidatePath -PathType Leaf)) {
        return $false
    }

    if (Test-IsIgnoredPath -CandidatePath $CandidatePath) {
        return $false
    }

    $extension = [System.IO.Path]::GetExtension($CandidatePath).ToLowerInvariant()
    return $allowedExtensions -contains $extension
}

function Get-FormattableFilesFromPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RequestedPath
    )

    $candidate = if ([System.IO.Path]::IsPathRooted($RequestedPath)) {
        [System.IO.Path]::GetFullPath($RequestedPath)
    }
    else {
        [System.IO.Path]::GetFullPath((Join-Path $repoRoot $RequestedPath))
    }

    if (-not (Test-Path -LiteralPath $candidate)) {
        throw "Path not found: $RequestedPath"
    }

    if (Test-IsIgnoredPath -CandidatePath $candidate) {
        return @()
    }

    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        if (Test-IsFormattableFile -CandidatePath $candidate) {
            return @($candidate)
        }

        return @()
    }

    return @(Get-ChildItem -LiteralPath $candidate -Recurse -File |
        Where-Object { Test-IsFormattableFile -CandidatePath $_.FullName } |
        Select-Object -ExpandProperty FullName)
}

function Get-ChangedFiles {
    Push-Location $repoRoot
    try {
        $paths = @()
        $paths += @(git diff --name-only --cached --diff-filter=ACMR)
        $paths += @(git diff --name-only --diff-filter=ACMR)
        $paths += @(git ls-files --others --exclude-standard)

        return @($paths |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            ForEach-Object { [System.IO.Path]::GetFullPath((Join-Path $repoRoot $_)) } |
            Where-Object { Test-IsFormattableFile -CandidatePath $_ } |
            Sort-Object -Unique)
    }
    finally {
        Pop-Location
    }
}

try {
    $clangFormat = Resolve-ClangFormat

    $files = if ([string]::IsNullOrWhiteSpace($Path)) {
        Get-ChangedFiles
    }
    else {
        Get-FormattableFilesFromPath -RequestedPath $Path |
            Sort-Object -Unique
    }

    if ($files.Count -eq 0) {
        Write-Host 'No formattable files found.'
        exit 0
    }

    foreach ($file in $files) {
        Write-Host "Formatting $(Get-RelativePath -AbsolutePath $file)"
        & $clangFormat -i $file
        if ($LASTEXITCODE -ne 0) {
            throw "clang-format failed for $file"
        }
    }

    exit 0
}
catch {
    Write-Error $_
    exit 1
}