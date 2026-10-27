// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QIcon>

class QFileInfo;
class QWidget;

enum class UiIcon {
    File, NewFile, Folder, Open, Save, SaveAll, Close, CloseAll, Undo, Redo,
    Cut, Copy, Paste, Trash, Search, Replace, SearchFiles, ZoomIn, ZoomOut,
    Indent, Unindent, Wrap, Paragraph, IndentGuide, Refresh, Print, Settings,
    Record, Play, Repeat, MacroSave, MacroEdit, Terminal, Info, Theme, Keyboard,
    Vim, Note, SplitHorizontal, SplitVertical, Font, Layout, Markdown, Dismiss, Menu, Plus
};

QIcon uiIcon(UiIcon kind, QWidget *context = nullptr);
QIcon documentTabIcon(const QFileInfo &file, bool modified, bool readOnly, QWidget *context = nullptr);

// Install after actions have been created. Refresh after applying a new theme;
// both are idempotent and preserve action text, shortcuts and behavior.
void installUiIcons(QWidget *root);
void refreshUiIcons(QWidget *root);
