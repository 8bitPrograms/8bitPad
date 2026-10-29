# 8bitPad 1.0.0 validation

## Pixel icon and direct Windows association — 2026-10-27

The approved cobalt tile now uses the same 16 × 16 outer geometry and stepped
corners as 8bitShell, with charcoal text bars and transparent surroundings.
SVG, PNG, nine-size Windows ICO and macOS ICNS assets were refreshed. The nine
icon images embedded in the Windows EXE match the source ICO byte for byte.

Associate selected file types applies requested per-user mappings without
launching Settings or an Open With picker. The effective handler is checked
with AssocQueryStringW before a type is marked Default. Protected or unverified
types are listed inside Pad as needing Windows confirmation; ineffective
mapping changes are restored. Previous mappings are retained for owned cleanup.
UserChoice values, hashes, permissions and machine policies are unchanged.

All 15/15 CTest suites passed, including isolated association fixtures for
protected choices, direct mappings, registration-only mode, verification failure,
cleanup/restoration, repeat Apply and one/six/nine pending-type UI batches.
The fresh installer payload also passed its SDK-free smoke check. Fixtures use
temporary stores and injected Shell queries; validation did not change the
workstation's live file associations.

Portable: dist/8bitPad-windows-x64-20261027-213202
MSI: dist/local-install-pixel-20261027-213217/8bitPad-1.0.0-windows-x64.msi
MSI SHA256: 81494d367bfb696a8b9551efd57a7e7acf3fefd242ae89c0b7079138eb9d3a8e

Compiled MSI metadata, shared Start Menu group, uninstall shortcut, icon stream
and empty-folder cleanup were checked. The ignored local update helper now
uses this MSI and its verified EXE checksum; syntax and read-only plan passed.
Actual desktop installation remains an operation from normal PowerShell.

Windows API references:
https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-assocquerystringw
https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-iapplicationassociationregistration

## Running-app update preflight — 2026-10-27

The subsequent native MSI update correctly entered reinstall mode, then failed
with 1603 because 8bitPad.exe was still running (PID 28884). The log explicitly
reported the executable in use, followed by Error 1310 while writing that file.

The local helper now waits up to three minutes for the selected installed
application to exit normally before invoking MSI. It identifies the executable
path, ignores a separate portable copy, and never force-closes the process.
A timeout reports the running PID before any MSI transaction starts.

All five installer-helper regression tests passed, including wait/release,
portable-copy filtering, timeout behavior and the earlier 1638 update paths.
The actual desktop installation still needs the app to be closed and the helper
run in the user's normal PowerShell.


## Same-version local MSI update correction — 2026-10-27

The user's external installation log reported product state 5 (already installed)
and PackagecodeChanging=1, but no REINSTALL properties. The local helper's single
HKCU uninstall-key check had missed the existing per-user product, causing 1638.

The helper now queries MsiQueryProductStateW, sets REINSTALL=ALL and
REINSTALLMODE=vamus for an existing product, and handles one service-side 1638
with the same verified package in in-place reinstall mode. It retains both logs
and never adds an uninstall step. Other errors, including 1601, are not retried.

The reusable planner/runner is scripts/ci/windows-msi.ps1 in each repository.
Three isolated regression tests passed: first-install/advertised states, an
installed product with no uninstall registry key, and bounded 1638 recovery
including repeated conflicts and access-denied service errors. No real MSI
installation, profile or association changes were performed by these fixtures.
The local helper's PowerShell syntax and read-only plan mode also passed.

Microsoft's update guidance:
https://learn.microsoft.com/en-us/windows/win32/msi/applying-small-updates-by-reinstalling-the-product

The user's actual MSI update still needs to run from normal PowerShell; this
managed session does not share the same product-registration visibility and
previously could not connect to the Windows Installer service.


## Cobalt icon and Windows resource refresh — 2026-10-27

The selected 8bitPad mark is cobalt blue (#2563EB) with light text bars.
The former pale square background plate is removed. SVG, PNG, nine-size Windows
ICO and macOS ICNS assets are updated with transparency.

An incremental Windows build was found to copy the new ICO without recompiling
version.rc, leaving the old icon inside the EXE. The resource now explicitly
depends on the generated ICO for Ninja builds. After rebuilding, all nine
embedded Windows icon images match the current source ICO byte for byte.

All 15/15 CTest suites passed. A fresh portable payload passed its SDK-free
application smoke check, and the version 1.0.0 MSI was rebuilt. The compiled MSI
icon stream matches the current ICO exactly; product metadata, Start Menu and
uninstall entries were checked again.

The ignored local install helper now supports selecting 8bitPad and refreshes
Explorer's icon notification after successful installation. The previously
recorded managed-session Windows Installer access restriction still applies;
these checks do not claim the new MSI is installed on the workstation.


## Local installable build — 2026-10-27

Both applications were rebuilt and tested from the current working trees:
8bitShell 21/21 CTest suites, 8bitPad 15/15. Fresh runtime payloads passed
SDK-free startup checks. The local x64 version 1.0.0 MSI packages were compiled
using cached WiX 6.0.2 after converting generated authoring; the CI authoring
remains WiX 3 compatible. Their compiled database tables were checked for the
correct product/version, per-user scope, 8bitPrograms Start Menu group,
application/uninstall shortcuts and empty-folder-only uninstall cleanup.

The local NSIS compiler could not start (Windows DLL relocation error), so the
local install artifacts are MSIs. The prepared EXE workflow still requires
validation on a suitable build host.

Installation from the managed session stopped before installing files:
Windows Installer could not connect to its service, returning 1601 with
access denied 0x80070005. Actual desktop installation/uninstallation is therefore
still pending. A checksum-verified helper for both local MSIs is under the
ignored 8bitShell build/maintenance directory, for execution from the user's
normal PowerShell.

Build trees, packages, SDK caches and this generated helper are ignored by Git.


## Local build guide and terminal-folder fix — 2026-10-27

The documented Windows x64 `build-windows.cmd -Diagnose` and
`build-windows.cmd -Package -Jobs 4` flows passed with the relocated Qt 6.11.2
SDK and matching Core5Compat overlay. All **15/15** CTest suites passed, including
custom 8bitShell directory-argument regression coverage.

A fresh portable application passed its application smoke check with SDK paths
removed and a temporary profile. The matching Shell portable build separately
verified that its actual terminal child starts in the requested directory even
when the default profile specifies another folder.

See [Building.md](Building.md) for build/test commands, package locations and
the manual Open Shell acceptance check. These Windows checks do not establish
hosted Linux/ARM/macOS or installer lifecycle validation.


Verified locally on 2026-10-27 after migration to this new checkout.

- Fresh Windows x64 Release build succeeds with MSVC and Qt 6.11.2.
- All 15/15 CTest suites pass.
- The release contract suite has 10 tests: 9 pass, with the POSIX-only fixture skipped on Windows. Its Windows fixture checks x64, ARM64 and x86 MSI authoring without installing test packages.
- Fresh Windows ZIP and tar.gz packages are in dist/release. Their SHA256 digests match the target manifest.
- The packaged app starts with SDK/plugin variables removed and System32-only PATH.
- Source scanning finds no deprecated application names or nested VCS metadata.
- The existing new origin is git@github.com:8bitPrograms/8bitPad.git. Previous repository history was not imported.
- Local SDKs and caches are ignored; build paths point to the new checkouts. Upstream source and license terms are retained.
- Runtime settings use only the current app namespace or an explicit profile override. Unrelated personal profile directories are neither adopted nor deleted.
- Both installers use the shared 8bitPrograms Start menu group, product-specific uninstall shortcuts and normal Windows Installed apps registration.

The native Linux/ARM/macOS matrix, experimental 32-bit builds, compiled MSI/EXE lifecycle tests, Windows signing and Apple signing/notarization still need their corresponding hosted/distribution checks. Preparing these flows does not claim those checks passed locally.

The new local Git history starts from the migrated source. No public release is created by this migration. See RELEASING.md for release setup and publication checks.

The packaged editor smoke check passes its editor, theme, recovery and workspace checks. The retained local MSI helper has 26 structural checks; no fixture MSI was installed.

## 2026-10-28: native command launcher and optional updates

- Windows x64 Release: 17/17 CTest suites pass. The native `8p` fixture preserves literal arguments and the invoking directory, including spaces, Unicode, quotes and shell punctuation.
- Single-instance identity includes the selected profile. Relative file/folder arguments are made absolute before forwarding; invalid incoming command options do not terminate the running editor.
- The managed host denies user-local named pipes. The application smoke report explicitly skips `command-line-live-handoff` for that capability denial. Live forwarding is not claimed as verified here; [the installed desktop verifier](Updates.md#desktop-verification) requires it to pass from normal Windows PowerShell.
- Offline updater fixtures verify stable versions, architecture/repository selection, exact checksums, damaged downloads, optional defaults and skip behavior. Automatic installation is disabled by default; these tests do not install anything or access live releases.
- The Python distribution/MSI helper suites run 15 tests: 14 pass and the POSIX fixture skips on Windows.
- A fresh portable app starts with SDK paths removed. The compiled local MSI includes `8p.exe`, the updater helper, user PATH install/removal actions and the existing Start menu/uninstall integration. All nine cobalt icon frames are embedded in the tested EXE. Build artifacts remain ignored.

Local MSI installation and the live command handoff need normal Windows PowerShell; the managed host cannot access the user's Windows Installer service. Published-release upgrade/rollback, other platforms and signing remain separate checks. No public release was created by this work.

### Desktop verification follow-up: Preferences font feedback

The first normal-desktop verification installed both apps successfully. Pad's
runtime log received the `8p` request and opened the file, then the font restore
in the later Preferences checks crashed with `0xC00000FD`. Inspection of the
local crash dump found hundreds of repeated font-combo/settings signal calls.

The font picker now blocks its own signals when reflecting settings, and ignores
selections whose family already matches the saved setting. A requested font
that is unavailable remains saved instead of being replaced by the picker's
fallback. This prevents the synchronous write-back loop.

The regression fails against the old binding and passes against the fix. It
checks bounded notifications, preservation of an unavailable font, no reflected
write-back, and successful user font selection. All 17 Windows x64 CTest suites
and fresh portable/staged runtime checks pass after the fix. The local Pad MSI
and maintenance helper were rebuilt/refreshed. Full installed desktop verification
must be repeated with that package; no new installation was attempted from the
managed host.

### Version taps and Cyberpunk palettes

Settings display the actual application version. Seven consecutive clicks reveal
Blueberi, Raspberi, Silverhand, V and K; a gap longer than 1.5 seconds resets the
sequence. Unlocking persists per profile and keeps the active theme. Saved bonus
themes remain usable even before the remaining bonus choices are revealed.

All 17 Windows x64 suites pass with the new behavior. Tests exercise the real
version button, six-versus-seven taps, timeout reset, restart persistence, and
unchanged selection. All 29 palettes run through editor/workspace/Markdown theme
checks. The three Cyberpunk palettes have seven distinct syntax role colors,
each with at least 4.5:1 contrast against its background. Actual C++ and Python
lexer colors are checked, along with unchanged document data and editing state.
Fresh portable and MSI payloads include the changes; desktop installation remains
the user's normal PowerShell operation.

### Bonus-theme visibility and concept chrome follow-up

Fixed the unfiltered Color theme menu and its command-palette entries. All five
bonus themes are hidden and disabled everywhere until the seven-click unlock;
saved bonus themes keep rendering but are not offered while locked. Tests open
the actual command palette and inspect the menu before and after unlocking.

The three Cyberpunk themes now use saturated menu bars and active tabs,
contrasting cyan/ice-blue panel accents, and matching status strips. Dark icon
backings and on-accent selection text keep bright surfaces legible. Other
palettes keep their existing chrome. The 17 Windows x64 suites pass, including
rendered header/tab checks, selection contrast, all-theme editor/Markdown checks,
and screenshots of the three themes with the C++ lexer active.

### File-type icons for Windows Explorer

Added 42 cobalt pixel-style icon families covering all 253 catalog suffixes.
The generated ICO files include eight native sizes from 16 to 256 pixels. The
application icon stays the first resource and its original image bytes are
preserved. Application registration uses index zero because negative one is a
reserved count query in Windows icon extraction APIs; document resources use
stable negative IDs starting at 201.

All 19 Windows x64 CTest suites pass. New tests load and render the actual EXE
resources on light and dark backgrounds, and directly exercise the native COM
handler across every suffix without writing system associations. They check
COM lifetime, bounds, no external CRT/Qt imports, ownership-preserving startup
repair, unchanged default-app choices, and static fallback behavior.

Both shared distribution test suites pass (10 tests each, one platform skip),
as do the 28 legacy MSI structural checks. The checked-in artwork generator
passes its reproducibility check. Native Explorer registration/display on the
user's desktop remains an installed-app check; no workstation associations
were changed by these tests.

The rebuilt local MSI contains the native icon DLL and its per-user COM server
registration. Read-only MSI database checks verify those entries alongside
the application icon, PATH, Start menu and uninstall records. The maintenance
installer now selects this MSI and verifies the installed EXE and icon DLL
against the current build hashes. The staged runtime smoke check also passes.
