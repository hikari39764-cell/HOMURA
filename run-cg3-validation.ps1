param(
	[ValidateSet('Debug', 'Development', 'Release')][string]$Configuration = 'Debug',
	[string]$OutputDirectory = (Join-Path $env:TEMP 'homura-cg3-validation'),
	[switch]$Build
)

$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$taskOutput = Join-Path ([System.IO.Path]::GetFullPath($OutputDirectory)) $Configuration
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null

# インストール済みのVisual Studioからコンパイラを見つける
$taskVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$taskVsPath = & $taskVsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$taskVsPath) { throw 'Visual Studio C++ tools were not found.' }
Import-Module (Join-Path $taskVsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $taskVsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

if ($Build) {
	& msbuild.exe (Join-Path $taskRoot 'HOMURA.vcxproj') /m /t:Build "/p:Configuration=$Configuration" /p:Platform=x64 /v:minimal *> (Join-Path $taskOutput 'engine-build.log')
	if ($LASTEXITCODE -ne 0) { throw "Engine build failed; see $taskOutput\engine-build.log" }
}

# 古いobjを拾わないように、直前のプロジェクトLink入力を使う
$taskLinkLog = Join-Path $taskRoot "HOMURA\x64\$Configuration\HOMURA.tlog\link.command.1.tlog"
if (!(Test-Path -LiteralPath $taskLinkLog)) { throw 'Build this configuration first, or pass -Build.' }
$taskObjects = ((Get-Content -LiteralPath $taskLinkLog -TotalCount 1).TrimStart('^') -split '\|') |
	Where-Object { [System.IO.Path]::GetFileName($_) -ine 'main.obj' }
foreach ($taskObject in $taskObjects) {
	if (!(Test-Path -LiteralPath $taskObject)) { throw "Missing built object: $taskObject" }
}
if (!($taskObjects | Where-Object { [System.IO.Path]::GetFileName($_) -ieq 'ParticleRenderer.obj' })) {
	throw 'The last engine build does not contain ParticleRenderer; rebuild the updated project.'
}

$taskAssimpConfig = if ($Configuration -eq 'Debug') { 'Debug' } else { 'Release' }
$taskExe = Join-Path $taskOutput 'CG3Validation.exe'
$taskFlags = @('/nologo', '/EHsc', '/std:c++20', '/permissive-', '/utf-8', '/W3', '/WX', '/DUNICODE', '/D_UNICODE', '/D_WINDOWS', '/DUSE_IMGUI')
$taskFlags += ('/I"' + $taskRoot + '"')
if ($Configuration -eq 'Debug') { $taskFlags += '/MTd', '/D_DEBUG' }
elseif ($Configuration -eq 'Release') { $taskFlags += '/MT', '/DNDEBUG', '/O2' }
else { $taskFlags += '/MT', '/Od' }
$taskFlags += ('/Fo"' + (Join-Path $taskOutput 'CG3Validation.obj') + '"')
$taskFlags += ('/Fd"' + (Join-Path $taskOutput 'CG3Validation-compile.pdb') + '"')
$taskFlags += ('/Fe"' + $taskExe + '"')
$taskFlags += ('"' + (Join-Path $taskRoot 'Tests\CG3Validation.cpp') + '"')
$taskFlags += $taskObjects | ForEach-Object { '"' + $_ + '"' }
$taskFlags += '/link', '/SUBSYSTEM:CONSOLE', '/MACHINE:X64', '/DEBUG', '/IGNORE:4049', '/IGNORE:4098'
if ($Configuration -eq 'Debug') { $taskFlags += '/INCREMENTAL', '/OPT:NOREF', '/OPT:NOICF' }
else { $taskFlags += '/INCREMENTAL:NO' }
$taskFlags += ('/LIBPATH:"' + (Join-Path $taskRoot "externals\assimp\lib\$taskAssimpConfig") + '"')
$taskFlags += 'assimp.lib', 'zlibstatic.lib', 'kernel32.lib', 'user32.lib', 'gdi32.lib', 'winspool.lib', 'comdlg32.lib', 'advapi32.lib', 'shell32.lib', 'ole32.lib', 'oleaut32.lib', 'uuid.lib', 'odbc32.lib', 'odbccp32.lib', 'dxguid.lib'
if ($Configuration -eq 'Release') { $taskFlags += '/LTCG' }
$taskResponse = Join-Path $taskOutput 'compile.rsp'
# clの/link以降を同じ行へ置き、Link専用の引数を正しく渡す
($taskFlags -join ' ') | Set-Content -LiteralPath $taskResponse -Encoding utf8

Push-Location -LiteralPath $taskOutput
try {
	& cl.exe ('@' + $taskResponse) *> (Join-Path $taskOutput 'compile.log')
	if ($LASTEXITCODE -ne 0) {
		Get-Content -LiteralPath (Join-Path $taskOutput 'compile.log')
		throw "Validation build failed: $Configuration"
	}
} finally {
	Pop-Location
}

foreach ($taskDll in @('dxcompiler.dll', 'dxil.dll')) {
	Copy-Item -LiteralPath (Join-Path $taskRoot "x64\$Configuration\$taskDll") -Destination (Join-Path $taskOutput $taskDll) -Force
}

# Desktopは撮影せず、非表示ウィンドウで描画したRTVをReadbackする
$taskProcess = Start-Process -FilePath $taskExe -ArgumentList ('"' + $taskOutput + '"') -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru `
	-RedirectStandardOutput (Join-Path $taskOutput 'run.log') -RedirectStandardError (Join-Path $taskOutput 'run.err.log')
if (!$taskProcess.WaitForExit(60000)) {
	$taskProcess.Kill()
	throw "Validation timed out: $Configuration"
}
$taskProcess.Refresh()
Get-Content -LiteralPath (Join-Path $taskOutput 'run.log')
Get-Content -LiteralPath (Join-Path $taskOutput 'run.err.log')
Write-Output "CONFIGURATION=$Configuration EXIT=$($taskProcess.ExitCode) OUTPUT=$taskOutput"
exit $taskProcess.ExitCode
