# 8bitPad feature coverage

8bitPad is a GPL-3.0-or-later fork of [Notepad Next](https://github.com/dail8859/NotepadNext), built with native Qt widgets and Scintilla/Lexilla. It is intended for Windows and Linux. It is not a claim of complete Notepad++ compatibility.

## Implemented in the inherited editor

| Area | Available behavior |
| --- | --- |
| Documents | Tabs, horizontal/vertical splits, open/save/save as/save copy/save all, recent files, folder workspace tree, external modification/deletion detection. |
| Editing | Undo/redo, multiple selections, rectangular/column editing, indentation, line move/duplicate/join/split, sorting, duplicate/empty-line removal, case conversion, comments, clipboard HTML/RTF. |
| Languages | Bundled language definitions including contextual DevOps profiles, Scintilla/Lexilla highlighting, code folding, brace matching, word completion, auto-indentation, EditorConfig support. |
| Search | Literal, extended and regular-expression search, replace, match counts, search all open documents, marked text, bookmarks and result navigation. |
| Views | Line numbers, whitespace/EOL display, indentation guides, word wrap, zoom, full screen, dockable panels. |
| Automation | Record/replay/save/edit macros, repeat macros, embedded Lua and Lua console. |
| Output | Printing, HTML/RTF export, Base64 and URL encode/decode. |
| Sessions | Optional restoration of saved files, unsaved changes, new documents, caret/scroll positions and bookmarks; snapshots are taken every minute and when closing. |
| Text formats | UTF-8 with/without BOM; UTF-16 LE/BE with/without BOM; Qt legacy codecs including Windows-1254/1252, ISO-8859-9/1, Shift-JIS and GB18030. CRLF, LF and CR line endings. Existing encoding, BOM and line endings are preserved when saving. |

## 8bitPad additions and reliability work

- Optional Vi-style editing and a collection of open-source editor palettes, including Ayu. The shipped command subset and palette credits are documented with their implementations; this is not a full Vim/Neovim engine.
- Background folder search and replacement with masks, subfolders, literal/regex patterns, case and whole-word options, result navigation, cancellation and explicit replacement confirmation. Open files and files changed since preview are skipped; unique adjacent backups are retained.
- A filterable keyboard-shortcut editor with persistent assignments, shortcut removal and conflict checks for application commands.
- Dockable Function List and Document Map panels with background outline extraction, symbol filtering and click-to-navigate. Outline parsers are heuristic; analysis is capped at 20 MiB, 5,000 symbols and 4,096 overview rows.
- Atomic file saving: a successful temporary write is committed before replacing the original. Failed writes keep the document dirty.
- Stateful UTF-16 decoding across read chunks, including surrogate pairs spanning the 4 MiB boundary.
- Strict UTF-8 detection first, with uchardet fallback for invalid BOM-less UTF-8, explicit encoding conversion/reinterpretation, and rejection of saves containing characters the selected encoding cannot represent. Recovery snapshots always preserve Unicode display text separately from the selected save encoding.
- Reload reads and decodes a replacement document before discarding the current text and undo history.
- Session recovery writes new snapshots and an atomic manifest before collecting previous snapshots. Recovery data can be opened even if its original document was deleted.
- Session snapshots reuse unchanged document copies, using edit revisions to avoid rewriting every unsaved buffer each minute.
- Empty regex replacements make forward progress, and regex anchors/lookaround keep the original subject context.
- Regex count and replace-all process a single decoded document with one compiled expression; replace-all commits one undo group and supports explicit bounded ranges internally.
- Separate application identity and no upstream Notepad Next auto-update channel.
- Windows/Linux CMake and CTest workflow. A workflow definition is not evidence that hosted Linux CI has run.

## Workspace and interface update

Grouped languages with persistent right-click Favorites, workspace file operations, live Markdown preview, contextual DevOps profiles and categorized Preferences are described in [WORKSPACE.md](WORKSPACE.md). All theme choices remain available.

## Known compatibility gaps

| Notepad++ capability | Current boundary |
| --- | --- |
| Windows DLL plugins / Plugins Admin | Not supported. Notepad++ plugins depend on its Win32 host ABI; they cannot be loaded by this native cross-platform Qt application. Lua is the current extension mechanism. |
| Full Vim / Neovim | Not supported. Vi mode covers its documented command subset; Vimscript, Neovim plugins, complete text objects, registers and command-language compatibility are not implied. |
| Encoding detection | Legacy detection is heuristic and can be wrong for short/ambiguous files; use explicit Reload as Encoding when necessary. GB18030 input/output must pass an exact round-trip check; characters unsupported by the installed Qt codec, including supplementary-plane characters on some versions, are rejected. UTF-32/UTF-7 and every platform-specific Qt codec are not covered by regression tests. Folder search deliberately accepts only valid UTF-8 and BOM-tagged UTF-16 to avoid rewriting files using a guessed legacy encoding. |
| Exact regex compatibility | Uses Qt's regular-expression engine, not the Notepad++ engine. Pattern/replacement syntax and edge cases can differ. |
| User-defined languages / Npp config import | Bundled language definitions are available. Complete UDL editing/import and full Notepad++ settings/macros/session migration are not provided. |
| Function list / document map / compare plugins | Function List and Document Map are built in, with bounded heuristic parsing rather than compiler-grade symbol indexing. Compare/diff plugins are not bundled. |
| All shortcuts and preferences | Application-command shortcuts can be edited and persisted. Complete Notepad++ preference parity, native plugin commands and remapping every internal Scintilla/Vim key are not claimed. |
| Multi-gigabyte guarantees | Documents remain in memory. Regex, sorting and conversions can allocate additional full-document buffers and run synchronously. No streaming multi-GB edit guarantee or comprehensive performance benchmark is claimed. The test suite includes an informational roughly 1 MiB/100,000-match replace-all measurement. |
| Folder search limits | At most 10,000 files and 5,000 results per preview; files over 32 MiB, binary files, unsupported encodings and symlinks are skipped. Retained capture text is capped. Replacement is disabled for cancelled/truncated previews. Regex cancellation is checked between matches; one expensive regex evaluation cannot be interrupted mid-match. |
| Recovery guarantees | Snapshot interval is one minute. Changes since the last successful snapshot can be lost on abrupt termination. Disk-full, forced-power-loss and simultaneous-instance fault injection still require broader validation. |

## Platform and validation scope

Use Qt 6.5 or newer with Core, Gui, Widgets, Network, PrintSupport, Core5Compat, Concurrent, LinguistTools and Test; the distribution also needs SVG support for bundled icons. Use a C++20 toolchain. The Linux helper is `bash scripts/build-linux.sh`; SDK paths can be passed through `CMAKE_PREFIX_PATH`.

CTest covers concrete behaviors, including Unicode/BOM round trips, UTF-16 chunk boundaries, failed saves/reloads, zero-width regular-expression replacement, folder masks, malformed/binary/oversize skipping, unchanged BOM/EOL and backup bytes, changed/open-file refusal, and cancellation/result limits. The application smoke test also checks recovery of Unicode unsaved changes, deleted originals, new documents, selected save encodings, languages, bookmarks and caret positions; unchanged snapshot reuse; and retention of the previous session after a deliberately failed manifest commit. Passing these tests does not establish every inherited action or every Linux desktop environment. Runtime validation must distinguish Windows execution, hosted Linux CI, and manual desktop interaction.
