<# Package only our plugin and deployment/license information, not the host. #>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Tag)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = Split-Path $PSScriptRoot -Parent
$VersionText = Get-Content (Join-Path $Root 'common\stronghold_version.h') -Raw
$Version = [regex]::Match($VersionText, 'kPluginVersion\s*=\s*"([^"]+)"').Groups[1].Value
if ($Tag -cne "v$Version" -or $Tag -notmatch '^v\d+\.\d+\.\d+$') { throw 'Tag must match the source version exactly.' }
$Head = & git -C $Root rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve build commit.' }
$Tagged = & git -C $Root rev-parse "$Tag^{commit}"
if ($LASTEXITCODE -ne 0 -or $Head -ne $Tagged) { throw 'Release tag must exist and point at the checked-out commit.' }
$Plugin = Join-Path $Root 'dist\plugins\stronghold_stats.dll'
if (-not (Test-Path $Plugin)) { throw 'Build the plugin first.' }
$Stage = Join-Path $Root ".build\release\stage-$Tag"
if (Test-Path $Stage) { throw 'Package staging directory already exists; preserve it and choose a clean build.' }
New-Item -ItemType Directory (Join-Path $Stage 'plugins'), (Join-Path $Stage 'licenses') -Force | Out-Null
Copy-Item -LiteralPath $Plugin -Destination (Join-Path $Stage 'plugins\stronghold_stats.dll')
foreach ($Name in @('README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md')) { Copy-Item -LiteralPath (Join-Path $Root $Name) -Destination $Stage }
Copy-Item -LiteralPath (Join-Path $Root 'dist\部署说明.txt') -Destination $Stage
Copy-Item -LiteralPath (Join-Path $Root 'third_party\il2cpp-mod-template\LICENSE') -Destination (Join-Path $Stage 'licenses\il2cpp-mod-template-LICENSE.txt')
$Lock = Get-Content (Join-Path $PSScriptRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
[ordered]@{ version = $Version; commit = $Head; tag = $Tag; framework_commit = $Lock.arkframework_commit; zig_version = $Lock.zig_version; plugin_sha256 = (Get-FileHash $Plugin -Algorithm SHA256).Hash; plugin_bytes = (Get-Item $Plugin).Length } |
    ConvertTo-Json | Set-Content (Join-Path $Stage 'build-info.json') -Encoding utf8
$Release = Join-Path $Root '.build\release'
$Zip = Join-Path $Release "ArkACT-$Tag-windows-x64.zip"
if (Test-Path $Zip) { throw 'Refusing to overwrite an existing release package.' }
Compress-Archive -Path (Join-Path $Stage '*') -DestinationPath $Zip -CompressionLevel Optimal
"$((Get-FileHash $Zip -Algorithm SHA256).Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($Zip))" | Set-Content "$Zip.sha256" -Encoding ascii
Write-Host "Release package: $Zip"
