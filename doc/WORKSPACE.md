# Workspace, language favorites and Markdown

The October 2026 update keeps the classic menu/toolbar layout and the existing
editor themes. The main chrome uses theme-derived neutral surfaces with a small
active-tab accent; toolbar, document and file-type icons are scalable vectors.
Preferences has General, Editor, Files & Session, Appearance and Terminal pages. Its
settings still save immediately, including the full theme catalog selector.

## White default and file associations

New profiles start with **Classic Light**, a plain white editor and neutral grey
controls. The other 25 themes remain available; an existing valid theme selection
is retained. Choose Classic Light from Settings > Color theme > Light or from
Preferences > Appearance to change an existing profile.

Open **File > File Associations** to select several extensions, a whole group,
or **Select all types**. The all-types button includes entries hidden by a filter;
**Select visible** affects only the current filter. **Clear selection** clears
the entire selection. Multiple custom extensions can be added together. No
association changes happen until the apply/register button is pressed.

On Windows, **Associate selected file types** registers one selected batch in
Open With and the application's capabilities. **Make 8bitPad the default** is
enabled initially and applies per-user file-type mappings directly. The effective
handler is checked through the Windows Shell API before a row is marked **Default**.
No Settings window or default-app picker is opened automatically.

Windows can protect an existing default choice. Those rows show **Needs Windows
confirmation** inside Pad, rather than reporting a successful default change.
An ineffective mapping is restored immediately; protected UserChoice data,
hashes and permissions are never changed. Administrator privileges do not provide
a supported override for those protected choices. Registration alone is distinct
from becoming the default. Clearing the default checkbox registers Open With
support without changing default mappings.

Previous per-user mappings are retained for cleanup and restored only while
8bitPad still owns the mapping. A later change by another application is retained.
Keep a portable installation in its final folder before registering it, since
the executable path is recorded.

On Linux, registration uses the user's XDG data location. The optional default
operation applies once per distinct MIME content type and reports failures.
Extensions sharing a MIME type can share the same default application; for
example, changing text/plain can affect both .txt and .log. Folder defaults stay
with the file manager. File-manager integration controls are separate from the
selected extension batch.

## File explorer

Open a folder as a workspace, then right-click a file, directory or empty tree
area. New File accepts the exact filename including its extension (`notes.md`,
`main.py`, `.gitignore`, or a name without an extension); it never appends `.txt`.
New Folder, F2 rename, copy/cut/paste, Move to Folder, internal drag/drop, path
copying, opening the containing folder, and Recycle Bin/Trash are available.
Dragging moves; hold Ctrl to copy. Ctrl+C/X/V operate on files when the tree has
focus. The filesystem watcher and F5 refresh pick up external changes.

Mutations stay within the opened workspace. Existing destination items are not
overwritten. Moves use filesystem rename; cross-filesystem moves are not
supported. Directory moves update open descendant documents while retaining
unsaved text and undo history. Copies run in the background into a temporary
staging directory, can be cancelled, and publish only a completed copy. Symbolic
links/junctions are not followed for mutations. Trash requires confirmation.

## Windows file icons

Files associated with 8bitPad use the cobalt, pixel-style Pad mark with a
type-specific badge. The catalog includes 42 icon families (253 suffixes),
including separate SQL and PL/SQL, Python, JavaScript, TypeScript, JSON, YAML,
Markdown, logs, PowerShell and shell scripts. Unknown custom suffixes use TXT.
All icons contain native 16/20/24/32/48/64/128/256-pixel images and transparency.

On the first normal launch after an update, Pad refreshes its existing icon
registrations and tells Explorer to refresh its cache. Existing default-app
choices are retained. Registration through File Associations uses embedded
EXE resources; the shared Windows Open With application handler selects the
same resources through a bundled native DLL that inspects only the suffix,
never document contents. It needs no Qt or separate runtime installation.
If the DLL cannot run in native Explorer (for example, an x86 app on x64
Windows), the generic Open With route retains a static Pad document icon.
Per-type registrations still use their own static icons.

Portable/profile and offscreen test launches do not automatically modify the
system registrations. Explicit File Associations actions still work from a
portable copy. Both Windows MSI build paths own the native COM server
registration for repair and removal; portable copies register it when the
user applies associations. The artwork and mappings are reproduced with
`python scripts/generate-file-icons.py`; `--check` validates the checked-in
outputs. Resource IDs are persistent and must not be renumbered.

The native handler uses one per-user COM identity. If a portable copy takes over
associations after MSI installation, uninstalling that MSI can remove the shared
COM registration. Reapply associations from the remaining portable copy to
restore its generic Open With icons; its per-type static icons are unaffected.

## Open Shell

In the file explorer, right-click a folder and choose **Open Shell**, then either
the default terminal or another detected application. For a file, its containing
folder is used; empty workspace space uses the workspace root. This starts a
separate external application. 8bitPad has no embedded terminal or PTY service.

**Configure terminals…** opens **Preferences > Terminal**. Choose the persistent
default or press Refresh after installing a terminal. Choosing an application
directly from the context menu affects only that launch. A custom executable can
be configured for applications that are not detected automatically. Custom
arguments use one argument per line, with literal spaces and no outer quotes;
`{folder}` inserts the selected directory. For a Windows console executable such
as PowerShell, enable **Open a new console window**. Graphical terminals should
leave that option disabled. Missing saved applications remain selected until you
choose a replacement; they are not silently replaced by another terminal.

Detection is bounded and cached, based on known terminal executables and local
application registrations. It does not launch applications to probe them or scan
the whole disk. Preferences performs discovery only when its Terminal page is
opened. Windows Terminal opens a new window, using the selected directory as the
process starting folder. 8bitShell/8bitShell uses `--working-directory`; a terminal's
own profile can still override its startup directory. Custom arguments are passed
directly to the executable, not evaluated as a command by 8bitPad. Their meaning
is defined by the target application. For a custom Windows Terminal configuration,
use `-d` and `.` on separate argument lines; the process already starts in the
selected folder. Unknown Linux TerminalEmulator registrations contribute the
executable only; use Custom for additional profile or startup options.

## Languages and favorites

Languages remain in logical groups: C family, Scripting, Web, JVM, Systems &
Functional, Documents & Data, Databases, DevOps & Build, and alphabetical Other.
All inherited languages remain reachable. There are no preselected favorites.

Right-click a language and choose Add to Favorites or Remove from Favorites.
Keyboard users can select a language and press Menu or Shift+F10. Favoriting does
not change the document's language. The Favorites submenu appears only after a
selection is saved; favorites persist in the current profile. Clicking a language
normally selects its lexer and updates both its grouped and favorite entries.

## Markdown preview

Right-click a Markdown file in the explorer, or use View > Markdown preview
(`Ctrl+Shift+M`). The dock follows the open, unsaved buffer and updates after a
short editing pause. Switching to another Markdown tab follows that document;
switching to other languages leaves the current Markdown preview available.

The native renderer supports headings, tables, task lists, links, block quotes,
local raster images and syntax-colored fenced code. Contents navigates headings;
double-click a heading to navigate to its source line. Aa controls body/code font
and size, and the zoom control or Ctrl+wheel changes preview size. These choices
persist. Preview colors follow the selected theme, including code tokens.

The preview is bounded to 4 MiB. Local images and linked source files must be
within the Markdown file's directory; remote image loading, raw HTML, embedded
scripts, SVG, Mermaid and math rendering are not implemented. Code-block coloring
is a lightweight renderer, not the source editor's complete Lexilla grammar.

## DevOps profiles

GitHub Actions workflow paths, Azure Pipelines filenames, Ansible role paths and
conservative content patterns select tailored YAML profiles. Jenkinsfile selects
a Groovy-based profile, not YAML. Profiles can also be selected explicitly in the
Language menu. Profile keywords supplement ordinary word completion.

Language > DevOps assistance provides inspectable templates, an explicit Use
profile action, documentation links and on-demand checks. Templates are inserted
as text; the editor never executes their commands. Checks cover selected common
block-style YAML mistakes and are limited to 1 MiB. They are advisory, not a YAML
parser, full service-schema validator, LSP or Jenkins compiler. Generic YAML is
not assigned a dialect merely because its extension is `.yaml` or `.yml`.

Rules and examples are based on official documentation:

- [GitHub workflow syntax](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax)
- [Azure Pipelines YAML schema](https://learn.microsoft.com/en-us/azure/devops/pipelines/yaml-schema/)
- [Ansible playbooks](https://docs.ansible.com/projects/ansible/latest/playbook_guide/playbooks_intro.html)
- [Jenkins Pipeline syntax](https://www.jenkins.io/doc/book/pipeline/syntax/)
- [YAML 1.2.2](https://yaml.org/spec/1.2.2/)
