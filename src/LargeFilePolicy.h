// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>

// Runtime checks keep paste, reload, and shrinking a buffer consistent. These
// limits affect automatic decoration only; explicit searches and edits remain
// available, and no user preference is overwritten.
namespace LargeFilePolicy {
constexpr qint64 documentThreshold = 20LL * 1024 * 1024;
constexpr qint64 longLineThreshold = 64LL * 1024;

constexpr bool reduceAutomaticWork(qint64 bytes)
{
    return bytes >= documentThreshold;
}

constexpr bool skipLineScan(qint64 bytes)
{
    return bytes >= longLineThreshold;
}
}
