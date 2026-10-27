# 8bitPad 0.1.0 validation


## Classic Light and batch file associations — 2026-10-25

| Environment | Result |
| --- | --- |
| Windows x64, MSVC 2022, Qt 6.11.2 | 15/15 CTest suites; 220 focused QtTest cases. |
| Windows portable package, native Windows platform without SDK paths | 872/872 application assertions passed. |
| Fedora Linux 44 under WSL, Qt offscreen | 15/15 CTest suites; 221 focused QtTest cases; 872/872 application assertions. |

Fresh profiles and invalid saved IDs use Classic Light; valid saved themes remain
unchanged. All 26 themes are included in the application/Markdown/dialog checks.
Screenshots were captured from the Windows portable application using example
documents and a file-association dialog with injected operations.

Batch association tests use temporary INI/XDG storage and injected process,
system-settings and shell-notification callbacks. They cover selected-only
registration, hidden selections, custom types, preserved Windows defaults and
UserChoice values, one Settings launch, shared Linux MIME defaults and failures.
No live system defaults or file associations were changed by these checks.
Interactive OS-default approval and Linux desktop integration were not manually
exercised. Windows registration still requires user choices in Default apps.

## External Open Shell update — 2026-10-25

| Environment | Result |
| --- | --- |
| Windows x64, MSVC 2022, Qt 6.11.2 | 13/13 CTest suites; 196 focused QtTest cases; 806/806 application checks. |
| Fedora Linux 44 under WSL, Qt 6.11.2, offscreen | 13/13 CTest suites; 197 focused QtTest cases; 806/806 application checks. |

The external process helper verified exact argument and working-directory
round trips, including Unicode, spaces and shell punctuation, without launching
an interactive terminal. Other checks cover fixed Windows Terminal arguments,
custom/default persistence, missing applications, rejected batch files, safe
registration discovery, menu routing, lazy settings initialization, and all 25
theme palettes. An installer shortcut regression prevents Uninstall 8bitShell from
being listed as a terminal. Linux registrations never import an arbitrary launch
command; known applications use explicit terminal adapters.

Read-only detection on the Windows workstation found Windows Terminal, 8bitShell,
Windows PowerShell and Command Prompt. Screenshots of the actual Terminal settings
page were checked in dark and light themes. Interactive terminal profiles and a
Linux desktop session were not manually exercised. In particular, 8bitShell's own
profile may override its requested starting directory. The feature adds no
embedded terminal, terminal rendering engine or PTY dependency.

## Workspace and UI update — 2026-10-25

The selected design combines concept A's classic compact main window with
concept C's categorized Preferences. All 25 existing themes are retained.

| Environment | Current result |
| --- | --- |
| Windows x64, Visual Studio 2022, Qt 6.11.2 | 10/10 CTest suites passed; 171 focused QtTest cases, zero failures/skips. |
| Windows portable bundle, native `windows` platform, PATH limited to Windows/System32 | 776/776 application assertions passed with no developer SDK required. |
| Fedora Linux 44 under WSL, Qt 6.11.2, offscreen platform | 10/10 CTest suites passed; 776/776 application assertions passed. |

New checks cover exact-name file creation, no-overwrite/cancelled-copy behavior,
directory moves with dirty open documents, case-only renames, junction/symlink
boundaries, actual favorite context-menu interactions and persistence, contextual
DevOps rules, Markdown formatting/images/fonts/links, categorized settings and
all-theme control/preview contrast. Every theme is also checked for a visibly
painted active-tab underline and for ADS retaining the application stylesheet.

Dark/light Windows screenshots were captured from the actual packaged application
and visually inspected. Linux manual X11/Wayland desktop interaction was not run;
the Linux archive uses Fedora-compatible system Qt libraries, not a universal
AppImage. No remote CI, release publishing, signing or installer was performed.
The build and feature instructions for future agents are in `AGENTS.md` and
`WORKSPACE.md`.

## Earlier validation record — 2026-10-23

Validated on 2026-10-23 from the delivered source. 8bitPad is based on Notepad Next v0.15, commit `fc4c807cf2b755b37d40de27cf29a5268c7c2fc5`.

| Environment | Result |
| --- | --- |
| Windows x64, MSVC 2022, Qt 6.11.2 | All 5 CTest suites passed. |
| Windows portable bundle, native `windows` Qt platform, SDK paths removed and PATH restricted to System32 | Application launched successfully; 567/567 smoke assertions passed. |
| Fedora Linux 44 x86_64 under WSL, GCC 16.2.1, Qt 6.11.2, glibc 2.43 | All 5 CTest suites passed using the offscreen Qt platform. |
| Freshly extracted Fedora TGZ | Native Linux executable launched; 567/567 smoke assertions passed. No missing libraries in the tested environment. |

The four focused QtTest suites cover 83 cases including initialization/cleanup: EditorCore (28), VimMode (27), FolderSearch (12), and Navigation (16). The fifth CTest suite runs the full application. Checks cover Unicode/encoding and BOM round trips, atomic-save failures, regex replacement, folder-search safety, Vi editing, function extraction, all 25 palettes, 88 language entries including plain text, application tab activation, and session recovery.

The Windows screenshots supplied with this release come from the native Windows bundle. Linux was executed as a real Linux ELF under WSL with offscreen rendering; manual Wayland/X11 desktop interaction and every Linux distribution have not been verified. The Fedora archive uses system Qt and is not a universal Linux binary.

## Performance sample

The portable Windows smoke test loaded a synthetic 24 MiB document into the editor in approximately 44 ms, opened and decoded it from disk in approximately 243 ms, and changed its theme in approximately 134 ms. The extracted Fedora build measured approximately 64 ms, 368 ms, and 109 ms respectively; its input was on a Windows-shared WSL filesystem. These are individual local measurements, not general benchmarks or multi-gigabyte guarantees. See [PERFORMANCE.md](PERFORMANCE.md) for boundaries.

## Scope

Passing tests does not prove every inherited menu action or full Notepad++ compatibility. Windows DLL plugins, a complete Vim engine, complete UDL/settings import, and some Notepad++ preferences remain outside the delivered feature set. See [FEATURES.md](FEATURES.md) and [VIM.md](VIM.md). Hosted GitHub Actions has not been run. No application installer, code-signing certificate, automatic update service, or remote release was published.
