param(
    [string]$MirrorPath = 'C:\Loader_esp32wf',
    [string]$Fqbn = 'esp32:esp32:esp32c3:UploadSpeed=921600,FlashMode=dio,FlashSize=4M,PartitionScheme=custom,CDCOnBoot=default,DebugLevel=none',
    [switch]$KeepMirror,
    [switch]$AdoptLegacyMirror
)

$ErrorActionPreference = 'Stop'
$MirrorMarkerName = '.loader-esp32wf-build-mirror'
$MirrorMarkerContent = 'Dedicated Loader_esp32wf build mirror. Safe to refresh.'

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'tools/build_firmware.ps1 requires Windows because it uses robocopy and Arduino IDE paths.'
}

function Resolve-ArduinoCli {
    $candidates = @(
        'C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe',
        'C:\Program Files (x86)\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $cmd = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    throw 'arduino-cli not found. Install Arduino IDE 2.x or add arduino-cli to PATH.'
}

function Find-ReparsePoint {
    param([Parameter(Mandatory = $true)][string]$RootPath)

    $pending = New-Object 'System.Collections.Generic.Stack[string]'
    $pending.Push($RootPath)
    while ($pending.Count -gt 0) {
        $current = $pending.Pop()
        foreach ($entry in Get-ChildItem -LiteralPath $current -Force) {
            if (($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                return $entry.FullName
            }
            if ($entry.PSIsContainer) {
                $pending.Push($entry.FullName)
            }
        }
    }
    return $null
}

function Find-ReparseAncestor {
    param([Parameter(Mandatory = $true)][string]$Path)

    $current = $Path
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                return $item.FullName
            }
        }
        $parent = [System.IO.Directory]::GetParent($current)
        if ($null -eq $parent -or $parent.FullName -eq $current) {
            break
        }
        $current = $parent.FullName
    }
    return $null
}

$repoRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$mirrorFullPath = [System.IO.Path]::GetFullPath($MirrorPath)
$mirrorDriveRoot = [System.IO.Path]::GetPathRoot($mirrorFullPath)
$userHome = [System.IO.Path]::GetFullPath([Environment]::GetFolderPath('UserProfile'))
$repoPrefix = $repoRoot.TrimEnd('\') + '\'
$mirrorPrefix = $mirrorFullPath.TrimEnd('\') + '\'

if ($mirrorFullPath -match '[^\x00-\x7F]') {
    throw "MirrorPath must contain ASCII characters only: $mirrorFullPath"
}
if ($mirrorFullPath.TrimEnd('\') -eq $mirrorDriveRoot.TrimEnd('\')) {
    throw "Refusing to use a drive root as MirrorPath: $mirrorFullPath"
}
if ($mirrorFullPath -eq $userHome -or
    $mirrorFullPath -eq $repoRoot -or
    $mirrorFullPath.StartsWith($repoPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
    $repoRoot.StartsWith($mirrorPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "MirrorPath must be a dedicated directory outside the repository and user-home root: $mirrorFullPath"
}
$reparseAncestor = Find-ReparseAncestor -Path $mirrorFullPath
if ($reparseAncestor) {
    throw "Refusing to place a build mirror below a reparse point: $reparseAncestor"
}

$mirrorMarker = Join-Path $mirrorFullPath $MirrorMarkerName
if (Test-Path -LiteralPath $mirrorFullPath) {
    if (-not (Test-Path -LiteralPath $mirrorFullPath -PathType Container)) {
        throw "MirrorPath exists but is not a directory: $mirrorFullPath"
    }
    $mirrorItem = Get-Item -LiteralPath $mirrorFullPath -Force
    if (($mirrorItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing to use a reparse point as MirrorPath: $mirrorFullPath"
    }

    $hasMarker = Test-Path -LiteralPath $mirrorMarker -PathType Leaf
    $hasEntries = $null -ne (Get-ChildItem -LiteralPath $mirrorFullPath -Force | Select-Object -First 1)
    if (-not $hasMarker -and $hasEntries) {
        if (-not $AdoptLegacyMirror) {
            throw (
                "Refusing to mirror into a non-empty unmarked directory: $mirrorFullPath. " +
                'If this is an old mirror created by this script, rerun once with -AdoptLegacyMirror.'
            )
        }
        $requiredLegacyFiles = @(
            'Loader_esp32wf.ino',
            'http_update.h',
            'DEV_Config.cpp',
            'EPD_7in3e.cpp',
            'partitions.csv'
        )
        $missingLegacyFiles = @($requiredLegacyFiles | Where-Object {
            -not (Test-Path -LiteralPath (Join-Path $mirrorFullPath $_) -PathType Leaf)
        })
        if ($missingLegacyFiles.Count -gt 0) {
            throw "Legacy mirror signature is incomplete; missing: $($missingLegacyFiles -join ', ')"
        }
        Write-Warning "Explicitly adopting the validated legacy build mirror: $mirrorFullPath"
    }
    if ($hasMarker) {
        $markerValue = (Get-Content -LiteralPath $mirrorMarker -Raw).Trim()
        if ($markerValue -ne $MirrorMarkerContent) {
            throw "Mirror marker content is invalid; refusing to refresh: $mirrorMarker"
        }
    }

    $reparsePoint = Find-ReparsePoint -RootPath $mirrorFullPath
    if ($reparsePoint) {
        throw "Refusing to refresh a mirror containing a reparse point: $reparsePoint"
    }
}

$arduinoCli = Resolve-ArduinoCli
$robocopy = Get-Command robocopy -ErrorAction SilentlyContinue
if (-not $robocopy) {
    throw 'robocopy not found. This Windows build helper requires robocopy.'
}
$buildPath = Join-Path $mirrorFullPath '.build\esp32c3'

Write-Host "==> Mirror sketch to ASCII path: $mirrorFullPath"
if (Test-Path -LiteralPath $mirrorFullPath) {
    # The marker, boundary, and reparse-point checks above ensure this only
    # removes entries from a dedicated build mirror. -KeepMirror retains just
    # the Arduino build cache, never arbitrary extra files.
    foreach ($entry in Get-ChildItem -LiteralPath $mirrorFullPath -Force) {
        $preserveBuildCache = $KeepMirror -and $entry.Name -eq '.build'
        $preserveMarker = $entry.Name -eq $MirrorMarkerName
        if (-not $preserveBuildCache -and -not $preserveMarker) {
            Remove-Item -LiteralPath $entry.FullName -Recurse -Force
        }
    }
}

New-Item -ItemType Directory -Force $mirrorFullPath | Out-Null
Set-Content -LiteralPath $mirrorMarker -Value $MirrorMarkerContent -Encoding ASCII
$null = & $robocopy.Source $repoRoot $mirrorFullPath /MIR /XJ /R:2 /W:1 `
    /XD .git .github .build cloud_server tools output .playwright-cli .sisyphus `
        .serena .codebuddy .cursor .codex .claude .continue __pycache__ `
    /XF $MirrorMarkerName .env '*.pyc' 'loader_frontend_*.png'
if ($LASTEXITCODE -gt 7) {
    throw "robocopy failed with exit code $LASTEXITCODE"
}
# Reassert the marker in case a future robocopy version treats an excluded
# destination-only file differently during /MIR cleanup.
Set-Content -LiteralPath $mirrorMarker -Value $MirrorMarkerContent -Encoding ASCII

New-Item -ItemType Directory -Force $buildPath | Out-Null

Write-Host '==> Compile with arduino-cli'
Write-Host "    CLI : $arduinoCli"
Write-Host "    FQBN: $Fqbn"
Write-Host "    Src : $mirrorFullPath"
Write-Host "    Out : $buildPath"

& $arduinoCli compile `
    --fqbn $Fqbn `
    --build-path $buildPath `
    --warnings default `
    $mirrorFullPath

if ($LASTEXITCODE -ne 0) {
    throw "arduino-cli compile failed with exit code $LASTEXITCODE"
}

$binPath = Join-Path $buildPath 'Loader_esp32wf.ino.bin'
$elfPath = Join-Path $buildPath 'Loader_esp32wf.ino.elf'
$partitionsPath = Join-Path $buildPath 'partitions.csv'

Write-Host ''
Write-Host '==> Build complete'
Write-Host "    BIN : $binPath"
Write-Host "    ELF : $elfPath"
Write-Host "    Part: $partitionsPath"
