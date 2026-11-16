<#
.SYNOPSIS
    验证卫戍协议插件的纯逻辑测试与全部源文件语法。
#>
[CmdletBinding()]
param(
    [string]$ClPath,
    [string]$ImGuiDir = (Join-Path (Split-Path $PSScriptRoot -Parent) 'ArkFramework-main\third_party\imgui-1.91.9b'),
    [string]$SdkVer
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = $PSScriptRoot
$Out = Join-Path $Root '.build\verify'
New-Item -ItemType Directory -Path $Out -Force | Out-Null
$Sdk = 'C:\Program Files (x86)\Windows Kits\10'
if (-not $ClPath) {
    $cl = Get-Command cl.exe -ErrorAction SilentlyContinue
    if ($cl) { $ClPath = $cl.Source }
    else {
        $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $ClPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe' | Select-Object -First 1
        }
    }
}
if (-not $ClPath -or -not (Test-Path -LiteralPath $ClPath)) { throw 'MSVC x64 cl.exe not found; install VS C++ Build Tools or use -ClPath.' }
if (-not $SdkVer) {
    $SdkVer = Get-ChildItem (Join-Path $Sdk 'Include') -Directory |
        Where-Object { $_.Name -match '^10\.0\.\d+\.\d+$' -and
            (Test-Path (Join-Path $_.FullName 'um\Windows.h')) -and
            (Test-Path (Join-Path $Sdk "Lib\$($_.Name)\um\x64\kernel32.lib")) } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1 -ExpandProperty Name
}
if (-not $SdkVer) { throw 'Windows 10/11 SDK not found; install the SDK or use -SdkVer.' }
if (-not (Test-Path -LiteralPath (Join-Path $ImGuiDir 'imgui.h'))) { throw "Dear ImGui headers not found: $ImGuiDir" }
Write-Host "MSVC: $ClPath; SDK: $SdkVer; ImGui: $ImGuiDir"
$Vc = Split-Path (Split-Path (Split-Path (Split-Path $ClPath -Parent) -Parent) -Parent) -Parent

if (-not (Test-Path -LiteralPath $ClPath)) { throw "cl.exe not found: $ClPath" }
$env:INCLUDE = @(
    (Join-Path $Vc 'include'),
    (Join-Path $Sdk "Include\$SdkVer\ucrt"),
    (Join-Path $Sdk "Include\$SdkVer\um"),
    (Join-Path $Sdk "Include\$SdkVer\shared")
) -join ';'
$env:LIB = @(
    (Join-Path $Vc 'lib\x64'),
    (Join-Path $Sdk "Lib\$SdkVer\ucrt\x64"),
    (Join-Path $Sdk "Lib\$SdkVer\um\x64")
) -join ';'

$CommonIncludes = @(
    '-I', (Join-Path $Root 'include'),
    '-I', (Join-Path $Root 'common'),
    '-I', (Join-Path $Root 'stronghold')
    '-I', (Join-Path $Root 'diagnostics')
    '-I', (Join-Path $Root 'third_party\il2cpp-mod-template')
)
$Base = @('/nologo', '/std:c++20', '/EHsc', '/W4', '/utf-8')
$Failures = 0

function Invoke-Cl([string[]]$Arguments) {
    & $ClPath $Arguments 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "cl failed (exit $LASTEXITCODE): $($Arguments -join ' ')"
    }
}

$TestNames = @('stronghold_model_tests', 'stronghold_csv_tests', 'stronghold_xlsx_tests', 'stronghold_report_tests', 'il2cpp_runtime_tests', 'stronghold_capture_tests', 'stronghold_schema_tests', 'metadata_report_tests', 'stronghold_ui_tests')
foreach ($Name in $TestNames) {
    $Source = Join-Path $Root "tests\$Name.cpp"
    Write-Host "==== TEST $Name ===="
    Push-Location $Out
    try {
        $Arguments = $Base + $CommonIncludes + @(
            $Source,
            "/Fe:$Name.exe",
            "/Fo:$Name.obj"
        )
        if ($Name -in @('il2cpp_runtime_tests', 'stronghold_capture_tests', 'stronghold_schema_tests')) {
            $Arguments += (Join-Path $Root 'common\il2cpp_runtime.cpp')
            # Multiple translation units cannot share a single /Fo file.
            $Arguments = @($Arguments | Where-Object { $_ -notlike '/Fo:*' })
        }
        if ($Name -in @('stronghold_capture_tests', 'stronghold_schema_tests', 'metadata_report_tests')) {
            $Arguments = @($Arguments | Where-Object { $_ -notlike '/Fo:*' })
            $Arguments += @(
                (Join-Path $Root 'diagnostics\metadata_report.cpp'),
                (Join-Path $Root 'third_party\il2cpp-mod-template\il2cpp\il2cpp_api.cpp'),
                (Join-Path $Root 'third_party\il2cpp-mod-template\platform\il2cpp_module_windows.c'),
                '/TP'
            )
        }
        if ($Name -eq 'stronghold_ui_tests') {
            $Arguments = @($Arguments | Where-Object { $_ -notlike '/Fo:*' })
            $Arguments += @('-I', $ImGuiDir, '/DIMGUI_ENABLE_TEST_ENGINE',
                (Join-Path $ImGuiDir 'imgui.cpp'),
                (Join-Path $ImGuiDir 'imgui_draw.cpp'),
                (Join-Path $ImGuiDir 'imgui_tables.cpp'),
                (Join-Path $ImGuiDir 'imgui_widgets.cpp'))
        }
        Invoke-Cl $Arguments
        if ($Name -eq 'metadata_report_tests') {
            $MockDir = Join-Path $Out 'mock-runtime'
            New-Item -ItemType Directory -Path $MockDir -Force | Out-Null
            $MockDll = Join-Path $MockDir 'GameAssembly.dll'
            Invoke-Cl ($Base + $CommonIncludes + @('/LD',
                (Join-Path $Root 'tests\mock_metadata_runtime.cpp'),
                "/Fe:$MockDll", "/Fo:$MockDir\mock_metadata_runtime.obj"))
            & (Join-Path $Out "$Name.exe") $MockDll 2>&1 | Out-Host
        } else {
            & (Join-Path $Out "$Name.exe") 2>&1 | Out-Host
        }
        if ($LASTEXITCODE -ne 0) {
            $Failures++
            Write-Host "FAIL $Name"
        }
    } catch {
        $Failures++
        Write-Host "FAIL $Name : $_"
    } finally {
        Pop-Location
    }
}

$SyntaxIncludes = $CommonIncludes + @('-I', $ImGuiDir)
$Syntax = @('/nologo', '/std:c++20', '/EHsc', '/W4', '/utf-8', '/c')
$Sources = @(
    (Join-Path $Root 'stronghold\plugin.cpp'),
    (Join-Path $Root 'stronghold\capture.cpp'),
    (Join-Path $Root 'common\il2cpp_runtime.cpp'),
    (Join-Path $Root 'common\unity_input.cpp')
    (Join-Path $Root 'diagnostics\metadata_report.cpp')
    (Join-Path $Root 'third_party\il2cpp-mod-template\il2cpp\il2cpp_api.cpp')
    (Join-Path $Root 'third_party\il2cpp-mod-template\platform\il2cpp_module_windows.c')
)
foreach ($Source in $Sources) {
    Write-Host "==== SYNTAX $Source ===="
    Push-Location $Out
    try {
        $Object = (Split-Path $Source -Leaf) + '.obj'
        $Arguments = $Syntax + $SyntaxIncludes + @(
            '-DIMGUI_USER_CONFIG=<ark/imgui_config.h>',
            $Source,
            "/Fo:$Object"
        )
        Invoke-Cl $Arguments
    } catch {
        $Failures++
        Write-Host "FAIL syntax $Source : $_"
    } finally {
        Pop-Location
    }
}

Write-Host ''
if ($Failures -gt 0) {
    Write-Host "VERIFY FAILED: $Failures step(s) failed"
    exit 1
}
Write-Host 'VERIFY OK: stronghold tests passed and all plugin sources compiled.'
