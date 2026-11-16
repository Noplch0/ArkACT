<#
.SYNOPSIS
    构建卫戍协议专用插件 stronghold_stats.dll。
.DESCRIPTION
    使用 Zig / Clang GNU ABI 编译，以避免与共享 ArkImGui.dll 的 Itanium C++ ABI 不兼容。
    插件直接通过 ArkFramework 原生 Hook 采集游戏数据，不依赖 damage_meter.dll。
#>
[CmdletBinding()]
param(
    [string]$Zig,
    [string]$DistDirectory = 'dist',
    [string]$ArkImGuiImportLibrary,
    [string]$ImGuiDir = (Join-Path (Split-Path $PSScriptRoot -Parent) 'ArkFramework-main\third_party\imgui-1.91.9b')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = $PSScriptRoot
if (-not (Test-Path -LiteralPath (Join-Path $ImGuiDir 'imgui.h'))) {
    throw "Dear ImGui headers not found: $ImGuiDir (use -ImGuiDir or tools/prepare-dependencies.ps1)"
}
$IncludeDir = Join-Path $Root 'include'
$CommonDir = Join-Path $Root 'common'
$StatsDir = Join-Path $Root 'stronghold'

function Resolve-Zig([string]$explicit) {
    if ($explicit) {
        if (-not (Test-Path -LiteralPath $explicit)) { throw "Zig not found: $explicit" }
        return (Resolve-Path -LiteralPath $explicit).Path
    }
    $bundled = Join-Path $Root '.tools\zig\ziglang\zig.exe'
    if (Test-Path -LiteralPath $bundled) { return (Resolve-Path -LiteralPath $bundled).Path }
    $command = Get-Command zig -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($scope in @('User', 'Machine')) {
        $path = [Environment]::GetEnvironmentVariable('PATH', $scope)
        if (-not $path) { continue }
        foreach ($dir in ($path -split ';')) {
            if (-not $dir -or $dir -notmatch 'zig') { continue }
            $candidate = Join-Path $dir 'zig.exe'
            if (Test-Path -LiteralPath $candidate) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        }
    }
    $winget = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    if (Test-Path -LiteralPath $winget) {
        $found = Get-ChildItem -LiteralPath $winget -Recurse -File -Filter 'zig.exe' -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
        if ($found) { return $found }
    }
    return $null
}

$ZigExe = Resolve-Zig $Zig
if (-not $ZigExe) {
    throw '未找到 zig.exe。可执行 winget install zig.zig，或用 -Zig 指定路径。'
}
$ZigVersion = (& $ZigExe version 2>$null | Select-Object -First 1)
Write-Host "Zig: $ZigExe (version $ZigVersion)"
if (-not $env:ZIG_GLOBAL_CACHE_DIR) {
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $Root '.build\zig-global'
}

$Dist = [IO.Path]::GetFullPath($(
    if ([IO.Path]::IsPathRooted($DistDirectory)) {
        $DistDirectory
    } else {
        Join-Path $Root $DistDirectory
    }
))
$PluginDir = Join-Path $Dist 'plugins'
$ObjDir = Join-Path $Root '.build\zig'
New-Item -ItemType Directory -Path $PluginDir, $ObjDir -Force | Out-Null

$ImportLib = $ArkImGuiImportLibrary
if (-not $ImportLib) { $ImportLib = Join-Path $Dist 'libArkImGui.dll.a' }
if (-not (Test-Path -LiteralPath $ImportLib)) {
    throw "ArkImGui import library not found: $ImportLib"
}

$TargetFlags = @('-target', 'x86_64-windows-gnu', '-O2')
$DefineFlags = @('-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-DWIN32_LEAN_AND_MEAN')
$WarningFlags = @(
    '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-missing-field-initializers',
    '-Wno-cast-function-type-mismatch'
)
$IncludeFlags = @($IncludeDir, $ImGuiDir, $StatsDir, $CommonDir,
                  (Join-Path $Root 'diagnostics'),
                  (Join-Path $Root 'third_party\il2cpp-mod-template')) |
    ForEach-Object { '-I' + $_ }
$CxxFlags = @('-std=c++20') + $WarningFlags + $DefineFlags +
    @('-DIMGUI_USER_CONFIG=<ark/imgui_config.h>') + $IncludeFlags

function Invoke-Zig([string[]]$Arguments) {
    $processInfo = New-Object System.Diagnostics.ProcessStartInfo
    $processInfo.FileName = $ZigExe
    $processInfo.RedirectStandardOutput = $true
    $processInfo.RedirectStandardError = $true
    $processInfo.UseShellExecute = $false
    $processInfo.CreateNoWindow = $true
    $quoted = foreach ($argument in $Arguments) {
        if ($argument -match '\s') {
            '"' + ($argument -replace '"', '\"') + '"'
        } else {
            $argument
        }
    }
    $processInfo.Arguments = ($quoted -join ' ')
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $processInfo
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($stdout) { $stdout | Out-Host }
    if ($stderr) { $stderr | Out-Host }
    $global:LASTEXITCODE = $process.ExitCode
    if ($process.ExitCode -ne 0) {
        throw "zig $($Arguments[0]) failed with exit code $($process.ExitCode)"
    }
}

$script:ObjectIndex = 0
function Compile-Source([string]$Path) {
    $script:ObjectIndex++
    $leaf = [IO.Path]::GetFileNameWithoutExtension($Path)
    $object = Join-Path $ObjDir "$leaf$($script:ObjectIndex).o"
    Write-Host "Compile $Path"
    Invoke-Zig (@('c++', '-x', 'c++') + $CxxFlags + $TargetFlags + @('-c', $Path, '-o', $object))
    if (-not (Test-Path -LiteralPath $object)) {
        throw "Compiler did not produce: $object"
    }
    return $object
}

$objects = @(
    (Compile-Source (Join-Path $StatsDir 'plugin.cpp')),
    (Compile-Source (Join-Path $StatsDir 'capture.cpp')),
    (Compile-Source (Join-Path $CommonDir 'il2cpp_runtime.cpp')),
    (Compile-Source (Join-Path $CommonDir 'unity_input.cpp'))
    (Compile-Source (Join-Path $Root 'diagnostics\metadata_report.cpp'))
    (Compile-Source (Join-Path $Root 'third_party\il2cpp-mod-template\il2cpp\il2cpp_api.cpp'))
    (Compile-Source (Join-Path $Root 'third_party\il2cpp-mod-template\platform\il2cpp_module_windows.c'))
)
$output = Join-Path $PluginDir 'stronghold_stats.dll'
Write-Host "Link $output"
Invoke-Zig (@('c++') + $TargetFlags + @('-shared') + $objects + @($ImportLib, '-luser32') +
    @("-Wl,--out-implib,$ObjDir\libstronghold_stats.dll.a", '-o', $output))

Write-Host ''
Write-Host "Build complete: $output"
Write-Host '部署：把 stronghold_stats.dll 放进 ArkFramework 发布目录的 plugins 下。'
