# Vim mode

8bitPad has an optional, native modal editing controller on top of Scintilla.
Enable **Vim mode** from the editor's menu. Each editor opens in **NORMAL** mode;
the status bar shows the mode and command/search input. Disable Vim mode to
restore regular editing. The editor's Windows/Linux shortcuts remain available,
including Save and Find. `Ctrl+R` is redo in normal mode.

This is a documented Vim-style editing subset, **not full Vim or Neovim**. It
does not load `.vimrc`, Vim plugins, or Vimscript, and it does not start a shell.

## Implemented keys

| Area | Keys |
| --- | --- |
| Modes | `Esc`, `Ctrl+[`, or `Ctrl+C` returns to normal; `v` character visual; `V` line visual |
| Insert | `i` before cursor, `a` after cursor, `I` first nonblank, `A` line end, `o` new line below, `O` new line above |
| Movement | `h j k l`, arrow keys, `w b e`, `0 ^ $`, `gg`, `G`, `Ngg`/`NG` to line N |
| Counts | Counts on motions, operators, `x`, paste, undo, and search repeat; operator/motion counts multiply, e.g. `2d3w` |
| Delete | `x`/Delete, `dd`, `dw`, `de`, `d$`/`D`, and `d` followed by an implemented motion |
| Yank | `yy`/`Y`, `yw`, `y$`, and `y` followed by an implemented motion |
| Paste | `p` after cursor or line; `P` before cursor or line; understands character and line yanks |
| Visual | Motions extend selection; `y` yanks; `d` or `x` deletes; `v`/`V` switches selection style |
| History | `u` undo, `Ctrl+R` redo, `.` repeat the last supported change |
| Search | `/text` forward, `?text` backward, Enter searches, `n` next, `N` opposite direction; wraps at document boundaries |
| File commands | `:w`, `:q`, `:wq`, `:q!` followed by Enter; routed to the application's save/close handling |

Normal mode uses a block caret. Insert mode uses the editor's previous caret
style. A whole insert session, an open-line insertion, a counted delete or paste,
and a dot-repeat are each grouped as one undo operation. UTF-8 motions and visual
selections operate on Unicode scalars, including supplementary-plane characters;
they never deliberately split an encoded character. CRLF, LF and CR line endings
are retained by line operations.

The unnamed register is local to each editor and supports linewise and
characterwise data. It is separate from the system clipboard; use the regular
application Copy/Paste menu actions for clipboard access (`Ctrl+C` returns to
normal mode while Vim mode is enabled). A Vim search is literal
and case-sensitive and preserves the regular Find dialog's Scintilla search state.

## Deliberate limits

- No text objects, named registers, marks, macros in Vim syntax, block visual
  mode, change/replace operators, `f`/`t` motions, indentation operators, folds
  controlled by Vim commands, or Ex ranges/substitutions. Use the normal editor
  features for search/replace, macros, folds, indentation, and column editing.
- `w`, `b`, and `e` classify Unicode letters/numbers/underscore as words,
  punctuation as a separate class, and whitespace as separators. Empty-line
  stops and every edge case of Vim's native word engine are not reproduced.
- `o`/`O` create an empty line; they do not copy indentation themselves. Ordinary
  typing still passes through the editor's normal insert handling.
- Counts on initial insert commands such as `3i` are rejected visibly. A count
  before `.` can repeat a supported insertion instead.
- Dot-repeat supports normal-mode deletes, line/character pastes, and contiguous
  insert changes (including `o`/`O`). An insert which edits pre-existing text
  outside its new insertion, or a visual deletion, clears dot-repeat rather than
  replaying an unrelated edit. Visual selection shape is not recorded.
- Undo/redo after an edit does not erase the last repeatable change, as in Vim.
  Mouse interaction exits visual/command mode; the next modal motion uses the
  clicked caret. Vim visual selection should be made with `v`/`V`.
- Counts are capped at 100,000. A single repeated paste/insertion is capped at
  64 MiB to avoid accidental memory exhaustion. Existing files are not limited
  by this paste guard.
- Position steps follow Unicode scalars, not entire grapheme clusters: a
  combining accent or part of a joined emoji can be a separate motion target.

Unsupported commands display a status message and do not silently fall through
to text insertion. `:q` must preserve the application's dirty-document prompt;
`:wq` must close only after successful saving. `:q!` explicitly requests closing
without saving the current document.

## Integration and tests

`VimMode` accepts `ScintillaEdit*`; `ScintillaNext*` works through inheritance.
Construct one controller per editor, normally parented to that editor. The public
API is `setEnabled(bool)`, `isEnabled()`, and `modeName()`. Connect
`modeChanged(QString)` and `statusMessage(QString)` to the active editor's UI,
and `exCommand(QString)` to its existing file actions.

`tests/VimModeTest.cpp` exercises actual Scintilla widgets through Qt key events:
modal transitions, native insert handling, undo/redo, UTF-8 movement and selection,
counts, linewise paste, CRLF editing, read-only state, search state preservation,
command emission, unsupported input, repeat, and editor destruction during insert.
It can run with Qt's `offscreen` platform in CI. These tests do not replace a
physical keyboard/IME accessibility test on each desktop environment.
