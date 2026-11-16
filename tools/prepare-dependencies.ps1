<# Build only the matching shared ArkImGui dependency, never load/inject the game. #>
[CmdletBinding()]
param([string]$Zig)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = Split-Path $PSScriptRoot -Parent
$Lock = Get-Content (Join-Path $PSScriptRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
$Deps = Join-Path $Root '.build\deps'
$Framework = Join-Path $Deps 'ArkFramework'
New-Item -ItemType Directory $Deps -Force | Out-Null
function Invoke-Git([string[]]$Arguments) {
    & git @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "git failed: $($Arguments -join ' ')" }
}
if (-not (Test-Path $Framework)) {
    Invoke-Git @('init', $Framework)
    Invoke-Git @('-C', $Framework, 'fetch', '--depth', '1', $Lock.arkframework_repository, $Lock.arkframework_commit)
    Invoke-Git @('-C', $Framework, 'checkout', '--detach', 'FETCH_HEAD')
}
$Head = & git -C $Framework rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $Head -ne $Lock.arkframework_commit) { throw 'Dependency checkout does not match dependencies.lock.json; preserve it and use a fresh .build/deps directory.' }
$Dirty = & git -C $Framework status --porcelain --untracked-files=no
if ($LASTEXITCODE -ne 0 -or $Dirty) { throw 'Refusing to build a locally modified framework dependency.' }
foreach ($Header in @('plugin.h', 'imgui_config.h')) {
    $Local = [IO.File]::ReadAllText((Join-Path $Root "include\ark\$Header")).Replace("`r`n", "`n")
    $Upstream = [IO.File]::ReadAllText((Join-Path $Framework "include\ark\$Header")).Replace("`r`n", "`n")
    if ($Local -ne $Upstream) { throw "SDK mismatch with locked ArkFramework: $Header" }
}
if (-not $Zig) {
    $Command = Get-Command zig -ErrorAction SilentlyContinue
    if ($Command) {
        $Candidate = $Command.Source
        if ((& $Candidate version) -eq $Lock.zig_version) { $Zig = $Candidate }
    }
}
if (-not $Zig) {
    $Zig = Join-Path $Deps "zig-x86_64-windows-$($Lock.zig_version)\zig.exe"
    if (-not (Test-Path $Zig)) {
        $Zip = Join-Path $Deps "zig-$($Lock.zig_version).zip"
        Invoke-WebRequest -Uri $Lock.zig_url -OutFile $Zip
        if ((Get-FileHash $Zip -Algorithm SHA256).Hash -ne $Lock.zig_sha256) { throw 'Zig download SHA256 mismatch.' }
        Expand-Archive -LiteralPath $Zip -DestinationPath $Deps
    }
}
$Zig = (Resolve-Path -LiteralPath $Zig).Path
if ((& $Zig version) -ne $Lock.zig_version) { throw "Expected Zig $($Lock.zig_version)." }
$ImGui = Join-Path $Framework $Lock.imgui_directory
$Out = Join-Path $Root 'dist'
$Objects = Join-Path $Root '.build\arkimgui'
New-Item -ItemType Directory $Out, $Objects -Force | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $Root '.build\zig-global'
$Flags = @('-target', 'x86_64-windows-gnu', '-O2', '-std=c++20', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE', '-DWIN32_LEAN_AND_MEAN', '-DARK_IMGUI_BUILD', '-DIMGUI_USER_CONFIG=<ark/imgui_config.h>', "-I$Root\include", "-I$ImGui")
$Sources = @('imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp', 'backends\imgui_impl_dx11.cpp', 'backends\imgui_impl_win32.cpp')
$Compiled = @()
foreach ($Source in $Sources) {
    $Object = Join-Path $Objects (($Source -replace '[\\/]', '_') + '.o')
    Write-Host "Build ArkImGui: $Source"
    & $Zig c++ @Flags -c (Join-Path $ImGui $Source) -o $Object
    if ($LASTEXITCODE -ne 0) { throw "ArkImGui compile failed: $Source" }
    $Compiled += $Object
}
$Import = Join-Path $Out 'libArkImGui.dll.a'
& $Zig c++ -target x86_64-windows-gnu -O2 -shared @Compiled -ld3d11 -ldxgi -ldwmapi -lgdi32 -luser32 "-Wl,--out-implib,$Import" -o (Join-Path $Out 'ArkImGui.dll')
if ($LASTEXITCODE -ne 0) { throw 'ArkImGui link failed.' }
$Result = [ordered]@{ Zig = $Zig; ImGuiDir = $ImGui; ArkImGuiImportLibrary = $Import; FrameworkCommit = $Head; ZigVersion = $Lock.zig_version }
$Result | ConvertTo-Json | Set-Content (Join-Path $Root '.build\dependencies.json') -Encoding utf8
Write-Host 'Prepared pinned dependencies; manifest: .build/dependencies.json'
