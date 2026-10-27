// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QIcon>

class QFileInfo;

// Drawn at the requested size using the current palette, including after a
// light/dark theme switch. No platform shell overlays or fixed bitmap assets.
QIcon explorerFileTypeIcon(const QFileInfo &file);
