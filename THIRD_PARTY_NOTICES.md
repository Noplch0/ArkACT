# Licensing and third-party notices

ArkACT is distributed under GPL-3.0-or-later. See LICENSE for the GPLv3 text.
Existing third-party copyright notices and licenses continue to apply.

## ArkFramework SDK and common integration

- Source: https://github.com/wuyu8512/ArkFramework
- Build compatibility baseline: 94c616fc4c115603983a62fd0fc6f3928db482b1
- Framework license: GPL-3.0-or-later; see upstream LICENSE and licensing notes.
- This repository preserves the GPL-3.0-or-later policy documented by the project.
- The host, injector and shared ArkImGui runtime are not bundled in our release.

## il2cpp-mod-template (vendored, reduced backend)

- Source: https://github.com/Mr-Courage/il2cpp-mod-template
- Commit: ee2afbf528f73012c493277aff934f18440f21e6
- License: MIT, full original notice in third_party/il2cpp-mod-template/LICENSE.
- Changes and precise scope: third_party/il2cpp-mod-template/UPSTREAM.md.
- No standalone loader, injector or second ImGui implementation is included.

## Dear ImGui (build/test dependency only)

- Version 1.91.9b from the pinned ArkFramework source tree.
- Copyright (c) 2014-2025 Omar Cornut. License: MIT.
- License retained in the dependency at third_party/imgui-1.91.9b/LICENSE.txt.
- Tests compile ImGui; production dynamically imports the host's ArkImGui.
  Neither that DLL nor the dependency source is redistributed in this release.

## Client schema evidence

tests/fixtures/client-schema.txt is a metadata-only compatibility test subset,
not gameplay implementation, a game binary, or a full dump. See its README.
Original user reports, runtime snapshots, game files and local reverse-engineering
materials are intentionally excluded from the public repository and release.
