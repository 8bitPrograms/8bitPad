# 8bitPad file-type icons

These original 8bitPrograms assets use the accepted cobalt pixel Pad silhouette,
three pale text bars and a dark type badge. There is no folded page corner.
Letters are explicit pixel geometry, so no font installation or text renderer is
needed. Short labels such as JSN and YML stay readable at Explorer's 16 px size.

`catalog.json` owns stable resource IDs, labels, accent colors and suffix aliases.
The unknown/custom-extension fallback is TXT. SQL and PL/SQL have separate badges.
Windows embeds every ICO in `8bitPad.exe`; file associations use its quoted path
and a negative resource ID. Moving a separate icon directory cannot break them.

Reproduce or check the checked-in production assets with Python 3:

```powershell
python scripts/generate-file-icons.py
python scripts/generate-file-icons.py --check
python scripts/generate-file-icons.py --check --preview build/file-icon-preview.png
```

The generator uses only the standard library. Every ICO includes 16, 20, 24, 32,
48, 64, 128 and 256 px artwork, 32-bit color and a transparent outside silhouette.
Small images use native Windows DIB icon frames with an alpha channel and mask;
128 and 256 px frames use PNG. SVGs retain the same pixel geometry.

Do not renumber IDs: existing Windows registry entries persist across upgrades.
The application icon is group 1; file types start at group 201. Changes to any
production icon update `FileTypeIcons::revision()` for association-cache refresh.

The Windows file-icon resource test verifies the built executable's icon groups,
source frames and actual Windows icon extraction, including the application icon.

Artwork and generator: GPL-3.0-or-later, like 8bitPad. This directory contains no
third-party language logos or fonts.
