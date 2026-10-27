# Large document behavior

8bitPad uses the native Scintilla editing engine. File contents are loaded into
memory; this is not a streaming viewer for files larger than available RAM.
Automatic lexer styling is configured for the visible text rather than an eager
whole-document recolor.

At **20 MiB or larger**, runtime guards reduce automatic work:

* Selecting a word no longer searches the entire document for matching highlights.
* Autocompletion keeps language keywords and skips the two document-wide regex
  scans for matching words.
* The scrollbar keeps normal scrolling while skipping marker/match overlays.
* Automatic URL discovery is skipped.

At **64 KiB per line or larger**, selected-word highlighting, word-completion
boundary scans, and automatic URL regex scans are skipped on that line, even in smaller files. Explicit Find,
Replace, Mark, editing, saving, and language selection remain available.

These checks use the current document length, so pasting a large block, reloading
a growing log, and shrinking a buffer take effect immediately. User preferences
are not changed. Automatic features resume when the document falls below the
threshold. Previously drawn automatic selection/URL indicators are cleared.

Theme changes update a fixed number of style slots and Lua language style roles;
they do not copy, parse, or rewrite the document bytes. Widget repainting may
still incur platform-dependent display costs. Full-file user commands, opening,
encoding conversion, recovery snapshots, and disk writes necessarily have costs
that scale with document size.

## Measurement and validation

The optional application smoke helper in `tests/ThemeTest.cpp` exercises the real
editor and decorators with a generated 24 MiB buffer and a 96 KiB line. It checks
that document matching is skipped, language-keyword completion survives,
automatic features resume after shrinking, and text/undo/preferences stay intact.
It records the following `elapsedMilliseconds` fields in `smoke-results.json`:

* `performance-24mib-buffer-load`
* `performance-24mib-selection-guard`
* `performance-24mib-completion-guard`
* `performance-24mib-url-guard`
* `performance-24mib-theme-switch`
* `performance-long-line-guards`

The broader application smoke also records `performance-24mib-disk-open-timing`
for an explicit disk-file open. On 2026-10-23, a Windows release build using
Qt 6.11.2 and its offscreen platform recorded the following single-run samples:

| Operation | Elapsed milliseconds |
| --- | ---: |
| Load approximately 24 MiB into the editing buffer | 44.079 |
| Open approximately 24 MiB from a local test file | 256.288 |
| Selection guard | 0.007 |
| Completion guard | 0.012 |
| URL guard | 0.016 |
| Theme change with the large buffer present | 205.319 |
| 96 KiB line guards | 0.883 |

All checks in that run passed. These are local synthetic measurements, not
universal latency guarantees, a statistical benchmark, or a comparison with
Notepad++. They do not establish physical Linux display performance or peak
process RAM. Consult the generated `smoke-results.json` for current run details;
the source and configured thresholds alone do not establish measured performance.
