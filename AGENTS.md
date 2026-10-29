# Working on 8bitPad

Preserve the user's current edits and existing GPL/third-party notices. This is a C++20, Qt 6, Scintilla/Lexilla application. The Windows build uses CMake/Ninja/MSVC; qmake is not the build system.

## Windows build entry point

Run from the repository root in a native Windows shell:

```powershell
.\build-windows.cmd -Diagnose
.\build-windows.cmd
# Build, test, then create a fresh portable package:
.\build-windows.cmd -Package
```

The launcher starts Windows PowerShell with no profile. The script finds the MSVC environment and Visual Studio's bundled CMake/Ninja, so these tools need not already be on PATH. Do not report the SDK/tools as missing solely because `where cmake`, `where ninja`, `where qmake` or searches of `C:\Qt` and `D:\Qt` find nothing. Run `-Diagnose` first and read `tooling/windows.local.json` when present. If sandbox access is denied, distinguish that from a missing file and request the appropriate filesystem access.

Qt resolution order: `-QtRoot`, `BIT8PAD_QT_ROOT`, local JSON config, `tooling/qt-sdk`, sibling `8bitShell/build/tooling/qt-sdk`, standard Qt MSVC installs. Core5Compat can live in a matching overlay selected through `-QtCompatRoot`, `BIT8PAD_QT_COMPAT_ROOT`, local JSON config or `tooling/qt-compat`. Never modify the shared base SDK to add a module. Do not commit the local SDK/config or build artifacts.

Verified Qt 6.11.2 MSVC x64 layout:

- Base SDK: the ignored local config, `tooling/qt-sdk`, or the sibling `8bitShell/build/tooling/qt-sdk`.
- Matching Core5Compat: repository-local `tooling/qt-compat`.
- CMake/Ninja: Visual Studio 2022 Build Tools under `Common7\IDE\CommonExtensions\Microsoft\CMake`.
- Machine configuration: `tooling/windows.local.json` (ignored by Git).

For a different machine, pass an installed MSVC Qt SDK explicitly or create the local JSON file as described in `doc/BUILDING-WINDOWS.md`. Build in native Windows; the MSVC SDK cannot compile the Linux target from WSL.

## Dependencies, outputs and checks

The script uses the three pinned sources under `dependency-sources/` automatically and does not download them. Use `-FetchDependencies` only when intentionally testing the network-fetched dependency revisions. Leave existing user changes intact; do not reset the repository or replace it with an older source/archive.

- Build directory: `build/windows-release` (created automatically, ignored by Git).
- Executable: `build/windows-release/8bitPad.exe`. The raw executable needs Qt DLLs available through the SDK environment; prefer the portable package for normal use.
- CTest: fifteen suites at the time of these instructions. Check the actual CTest summary rather than assuming old counts.
- QtTest details: `build/windows-release/tests/*.xml`.
- Application checks: `build/windows-release/tests/smoke/smoke-results.json`.
- Portable packages: fresh `dist/8bitPad-windows-x64-<timestamp>` folders. Extract/copy the entire folder and launch `Start-Portable.cmd`.

`-Package` runs after successful tests and preserves existing packages and their profiles. Do not claim the build succeeded unless CMake/build and required tests actually succeed. If tests fail, inspect the XML/JSON reports. Do not weaken a test to hide a product bug. Current platform execution, source inspection, and Linux desktop validation are distinct claims.

## Workspace and UI conventions

The user selected classic compact concept A with concept C's categorized Preferences.
Keep the code editor's theme/font behavior intact. All 26 themes must drive the
chrome and Markdown preview. Never re-enable ADS's own stylesheet: it overwrites
the application style on palette changes. Use UiIcons for new toolbar/menu icons.
Languages stay grouped; only explicit user favorites belong in Favorites.
File move notifications must preserve unsaved buffers/undo via followFileMove,
not save/rename the editor a second time. See doc/WORKSPACE.md for exact limits.
Tests include all-theme UI/Markdown contrast and actual favorite context menus.

External terminals stay external: use ExternalTerminals for discovery and detached
launch, OpenShellMenu for context actions, and TerminalPreferences for settings.
Never interpolate folder paths into shell command strings. Windows Terminal uses
a fixed relative startup argument with the process cwd. Keep discovery bounded,
cached, and lazy. Tests launch only a purpose-built argument/cwd helper, not a real
terminal. Preferences and menu tests inject discovery/launch callbacks.

New profiles default to Classic Light. Preserve valid saved theme choices.
FileAssociations performs a single selected-extension batch; FileAssociationsDialog
owns the checkable/grouped selection UI. Windows registration is not proof of a
default change: apply requested per-user mappings directly and verify the effective
handler with the Shell API. Show protected choices inline without automatically
opening Settings; never manipulate UserChoice hashes or permissions. Linux works by MIME type, so aliases can share defaults.
Association tests must inject temporary storage, process and Shell-query callbacks;
never change the workstation's real associations as part of a build or screenshot run.

## Installers and release automation

Read `doc/RELEASING.md` for MSI and Linux runtime archive packaging. Linux releases
are ordinary `.tar.gz` bundles; the user explicitly rejected AppImage distribution.
Use `scripts/package-msi.ps1` only with a tested portable inventory and pinned
WiX 3.14.1. MSI uses per-user LocalAppData/Programs/8bitPad, a fixed UpgradeCode,
and incremented three-field versions. Preserve profiles/settings and do not install
on the workstation or change its associations as a packaging test.

`install.ps1` and `install.sh` use the same versioned GitHub release assets and
`SHA256SUMS.txt`. Their repository default is the temporary `OWNER/8bitpad`;
it must be replaced or overridden before real network installation. Keep their
fixture tests offline and isolated. The GitHub workflow can build packages on PRs
and branches, and prepares a draft only for a matching version tag after both
platform test jobs pass. Local lint is not evidence of a hosted CI run. Do not
publish a release or create a GitHub repository without the user's authorization.
