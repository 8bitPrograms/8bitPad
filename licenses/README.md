# Retained dependency notices

These are the retained notices and source-version records for the 8bitPad
build. They are copied into the Windows bundle and the CMake install tree.
Application and theme notices are also shipped separately as `LICENSE`,
`THIRD_PARTY_NOTICES.md`, and the theme notice directory.

| Directory | Source used by this build |
| --- | --- |
| `ads` | Qt Advanced Docking System v5.1.1, commit `023ce95934fecfd4cf5c672c9c404fabe0f54923`; LGPL-2.1-or-later notice retained from `src/DockManager.cpp` and the full upstream license. https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System |
| `singleapplication` | SingleApplication v3.3.4, commit `a3ed916f591c300e97b873fde36863fa37b49fa9`; original MIT license and source notice. https://github.com/itay-grudev/SingleApplication/tree/v3.3.4 |
| `editorconfig-core-qt` | Commit `ab62f0554abf2bbe4d45427b36a8b2f81ca7b4ab`; original MIT license. https://github.com/editorconfig/editorconfig-core-qt |
| `lua` | Lua 5.3.4; full copyright/permission notice retained verbatim from the bundled `src/lua.h`. https://www.lua.org/versions.html#5.3 |
| `scintilla`, `lexilla`, `uchardet` | Notices copied unchanged from the corresponding bundled source trees. |
| `qt` | Qt 6.11.2 Windows SDK module inventories and upstream notices, described below. |

## Qt inventory

`qt/sbom/` contains the original SPDX JSON inventories supplied with the
QtBase, QtSvg, and Qt5Compat 6.11.2 Windows SDK modules used for this build.
`qt/*-ATTRIBUTIONS.txt` retains their package copyright, declared/concluded
license expressions, source locations, and attribution metadata.

`qt/qtbase`, `qt/qtsvg`, and `qt/qt5compat` retain the referenced upstream
third-party notice files and attribution JSON. `qt/standard` contains the
standard license/exception texts, including Qt's LGPL-3.0 and GPL texts.
`qt/extracted` retains the SDK's custom license texts. `qt/SOURCES.json` records
the source URL and SHA-256 hash of every retrieved file and the copied SDK
inventories. QtBase and QtSvg files use the SDK's exact upstream revisions;
Qt5Compat's inventory omits a source revision, so its files use the matching
official `v6.11.2` tag.

The module inventories include tools, tests, and platform code beyond the
runtime DLLs/plugins selected by `windeployqt`; retaining an inventory entry
does not mean that component is included in the application bundle. The
inventories also record upstream license alternatives, including commercial
alternatives; this record does not assert that a commercial Qt license is used.

These notices are version-specific. Regenerate the inventory and retained
texts when updating the dependency versions or the bundled Qt SDK. Linux
builds use their distribution's Qt libraries and associated notices; the
Windows SDK inventory is not evidence of the version installed on Linux.
