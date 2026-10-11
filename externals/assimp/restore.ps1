param(
    [ValidateSet('Debug', 'Release', 'Development', 'All')]
    [string]$Configuration = 'All'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE."
    }
}

try {
    # MSBuildが別のPowerShell環境を継承しても、実行中の標準モジュールを使う
    $env:PSModulePath = [System.IO.Path]::Combine($PSHOME, 'Modules')
    Import-Module ([System.IO.Path]::Combine($env:PSModulePath,
        'Microsoft.PowerShell.Management', 'Microsoft.PowerShell.Management.psd1')) -Force
    Import-Module ([System.IO.Path]::Combine($env:PSModulePath,
        'Microsoft.PowerShell.Utility', 'Microsoft.PowerShell.Utility.psd1')) -Force

    $assimpRoot = $PSScriptRoot
    $versionPath = Join-Path $assimpRoot 'version.json'
    $version = Get-Content -LiteralPath $versionPath -Raw | ConvertFrom-Json
    $recipe = (Get-FileHash -LiteralPath $versionPath -Algorithm SHA256).Hash + ':' +
        (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
    $configurations = if ($Configuration -eq 'All') { @('Debug', 'Release') }
        elseif ($Configuration -eq 'Development') { @('Release') }
        else { @($Configuration) }

    # 同時ビルドで同じキャッシュへ書き込まないようにする
    $mutexName = 'Local\HOMURA-Assimp-' + (Get-FileHash -LiteralPath $versionPath -Algorithm SHA256).Hash
    $mutex = New-Object System.Threading.Mutex($false, $mutexName)
    $locked = $false
    try {
        try { $locked = $mutex.WaitOne(600000) }
        catch [System.Threading.AbandonedMutexException] { $locked = $true }
        if (-not $locked) { throw 'Timed out waiting for the Assimp build lock.' }

        $pending = @($configurations | Where-Object {
            $libraryDirectory = Join-Path $assimpRoot "lib\$_"
            $stamp = Join-Path $libraryDirectory 'recipe.txt'
            -not ((Test-Path -LiteralPath $stamp) -and
                ((Get-Content -LiteralPath $stamp -Raw).Trim() -eq $recipe) -and
                (Test-Path -LiteralPath (Join-Path $libraryDirectory 'assimp.lib')) -and
                (Test-Path -LiteralPath (Join-Path $libraryDirectory 'zlibstatic.lib')) -and
                (Test-Path -LiteralPath (Join-Path $assimpRoot 'include\assimp\Importer.hpp')) -and
                (Test-Path -LiteralPath (Join-Path $assimpRoot 'include\assimp\config.h')))
        })
        if ($pending.Count -eq 0) {
            Write-Host 'Assimp 6.0.2 is ready.'
            exit 0
        }

        $cmake = (Get-Command cmake -ErrorAction Stop).Source
        $tar = (Get-Command tar -ErrorAction Stop).Source
        $cache = Join-Path $assimpRoot '.cache'
        New-Item -ItemType Directory -Path $cache -Force | Out-Null
        $archive = Join-Path $cache "assimp-$($version.commit).tar.gz"
        $source = Join-Path $cache "assimp-$($version.commit)"
        $build = Join-Path $cache 'build'

        # 公式の固定コミットだけを取得し、内容をハッシュで確認する
        if (-not (Test-Path -LiteralPath $archive)) {
            [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
            Invoke-WebRequest -UseBasicParsing -Uri $version.url -OutFile $archive
        }
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $version.sha256) {
            throw "Assimp archive checksum mismatch: $archive"
        }
        if (-not (Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt'))) {
            Invoke-Checked $tar @('-xf', $archive, '-C', $cache)
        }

        # 本体と同じv143・静的CRTを使い、授業で使うOBJとglTFだけを有効にする
        $configureArguments = @(
            '-S', $source, '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'x64', '-T', 'v143',
            '-DBUILD_SHARED_LIBS=OFF', '-DUSE_STATIC_CRT=ON',
            '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>',
            '-DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF',
            '-DASSIMP_BUILD_OBJ_IMPORTER=ON', '-DASSIMP_BUILD_GLTF_IMPORTER=ON',
            '-DASSIMP_NO_EXPORT=ON', '-DASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT=OFF',
            '-DASSIMP_BUILD_TESTS=OFF', '-DASSIMP_BUILD_ASSIMP_TOOLS=OFF',
            '-DASSIMP_BUILD_SAMPLES=OFF', '-DASSIMP_BUILD_DRACO=OFF',
            '-DASSIMP_BUILD_ZLIB=ON', '-DASSIMP_WARNINGS_AS_ERRORS=OFF',
            '-DASSIMP_INSTALL_PDB=OFF', '-DASSIMP_INJECT_DEBUG_POSTFIX=OFF',
            '-DASSIMP_IGNORE_GIT_HASH=ON', '-DLIBRARY_SUFFIX:STRING='
        )
        Invoke-Checked $cmake $configureArguments

        foreach ($buildConfiguration in $pending) {
            Invoke-Checked $cmake @('--build', $build, '--config', $buildConfiguration,
                '--target', 'assimp', '--parallel', '4')
            $libraryDirectory = Join-Path $assimpRoot "lib\$buildConfiguration"
            New-Item -ItemType Directory -Path $libraryDirectory -Force | Out-Null
            Copy-Item -LiteralPath (Join-Path $build "lib\$buildConfiguration\assimp.lib") `
                -Destination (Join-Path $libraryDirectory 'assimp.lib') -Force
            $zlibName = if ($buildConfiguration -eq 'Debug') { 'zlibstaticd.lib' } else { 'zlibstatic.lib' }
            Copy-Item -LiteralPath (Join-Path $build "contrib\zlib\$buildConfiguration\$zlibName") `
                -Destination (Join-Path $libraryDirectory 'zlibstatic.lib') -Force

            $includeDirectory = Join-Path $assimpRoot 'include'
            New-Item -ItemType Directory -Path $includeDirectory -Force | Out-Null
            Copy-Item -LiteralPath (Join-Path $source 'include\assimp') `
                -Destination $includeDirectory -Recurse -Force
            Copy-Item -Path (Join-Path $build 'include\assimp\*.h') `
                -Destination (Join-Path $includeDirectory 'assimp') -Force
            Set-Content -LiteralPath (Join-Path $libraryDirectory 'recipe.txt') -Value $recipe -Encoding ASCII
        }
        Write-Host 'Assimp 6.0.2 restore completed.'
    }
    finally {
        if ($locked) { $mutex.ReleaseMutex() }
        $mutex.Dispose()
    }
}
catch {
    Write-Error $_
    exit 1
}
