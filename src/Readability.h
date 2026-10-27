// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>

double contrastRatio(const QColor &a, const QColor &b);
// Moves the foreground toward black or white until it clears minRatio on bg.
// Colors that are already readable are returned unchanged.
QColor liftContrast(const QColor &foreground, const QColor &background, double minRatio);
