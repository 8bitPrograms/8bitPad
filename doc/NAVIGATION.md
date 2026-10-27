# Function List and Document Map

Open **View → Function List** to see functions, methods and types in the current
file. Filter by name, then double-click a result or select it and press Enter to
jump to its original document line. The list follows the active tab and refreshes
after editing pauses.

The outline supports common declarations in C/C++, C#, Java, Python,
JavaScript/TypeScript, Go, Rust, Lua, Ruby, shell scripts, PowerShell and PHP.
Language selection and the file extension determine which parser is used.
Comments and ordinary quoted strings are excluded. This is a heuristic outline:
complex macro expansions, multiline parameter lists, language-specific string
syntaxes, and unusual declarations may be missed. It is not a language server
or a compiler symbol index.

Open **View → Document Map** for a compact overview of line lengths, indentation,
and blank space. The highlighted band tracks the visible part of the document.
Click or drag to navigate, or focus the map and use arrow, Page Up/Down or
Home/End keys. This is a text-density overview rather than a second editable
document or a tiny font rendering of every character.

Both panels start hidden. Parsing and overview generation use a bounded snapshot
on a worker thread, with a 400 ms editing debounce. Old results are discarded if
the document changes. Files above 20 MiB display a paused message; outlines are
capped at 5,000 symbols, and the map compresses documents to at most 4,096 rows.
Hidden panels do not start analysis. These limits apply only to the panels, not
to the editor's ability to open or edit a file. Closing the window cancels and
joins an active analysis worker before the application shuts down.

The map never shares an editable Scintilla document and cannot insert, paste,
drop, or delete text. Navigating deliberately focuses the original editor.

`tests/NavigationTest.cpp` verifies the pure extraction and overview helpers,
including language detection, comment exclusion, Unicode identifiers, line
endings, correct line numbers, size limits, and cancellation. It does not validate every
language grammar or replace native desktop interaction testing.
