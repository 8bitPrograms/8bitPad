// SPDX-License-Identifier: GPL-3.0-or-later
#include "Readability.h"

#include <algorithm>
#include <cmath>

namespace {
double channel(double value)
{
    value /= 255.0;
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double luminance(const QColor &color)
{
    return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green()) + 0.0722 * channel(color.blue());
}
}

double contrastRatio(const QColor &a, const QColor &b)
{
    const double left = luminance(a);
    const double right = luminance(b);
    const double hi = std::max(left, right);
    const double lo = std::min(left, right);
    return (hi + 0.05) / (lo + 0.05);
}

QColor liftContrast(const QColor &foreground, const QColor &background, double minRatio)
{
    if (!foreground.isValid() || !background.isValid())
        return foreground;
    if (contrastRatio(foreground, background) >= minRatio)
        return foreground;

    const QColor toward = luminance(background) > 0.4 ? QColor(0, 0, 0) : QColor(255, 255, 255);
    QColor best = foreground;
    for (int step = 1; step <= 16; ++step) {
        const double amount = step / 16.0;
        best = QColor(qRound(foreground.red() * (1.0 - amount) + toward.red() * amount),
                      qRound(foreground.green() * (1.0 - amount) + toward.green() * amount),
                      qRound(foreground.blue() * (1.0 - amount) + toward.blue() * amount));
        if (contrastRatio(best, background) >= minRatio)
            return best;
    }
    return best;
}
