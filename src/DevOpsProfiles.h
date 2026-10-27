// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>

namespace DevOpsProfiles {
enum class Kind { None, GitHubActions, AzurePipelines, Ansible, Jenkins };
struct Snippet { QString title; QString text; };
struct Diagnostic { int line; QString message; }; // one-based line; advisory, not schema validation

// Content detection is bounded internally to 64 KiB and deliberately conservative.
Kind detect(const QString &filePath, const QString &contents = {});
QString languageName(Kind kind);
Kind kindForLanguage(const QString &language);
QList<Snippet> snippets(Kind kind);
QString documentationUrl(Kind kind);
// Limited checks for common block-style YAML mistakes. No parser, schema, LSP,
// plugin/task validation or Jenkins compilation is implied by an empty result.
QList<Diagnostic> check(Kind kind, const QString &contents);
}
