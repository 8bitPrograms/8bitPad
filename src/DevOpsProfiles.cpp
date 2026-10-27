// SPDX-License-Identifier: GPL-3.0-or-later
#include "DevOpsProfiles.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <utility>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("DevOpsProfiles", text); }

struct YamlLine {
    int line = 0;
    int indent = 0;
    int keyIndent = 0;
    bool sequence = false;
    QString key;
    QString value;
    bool tabIndent = false;
};

bool quoteCloses(const QString &text, QChar quote, int start)
{
    for (int i = start; i < text.size(); ++i) {
        if (quote == QLatin1Char('"') && text[i] == QLatin1Char('\\')) {
            ++i;
        } else if (text[i] == quote) {
            if (quote == QLatin1Char('\'') && i + 1 < text.size() && text[i + 1] == quote)
                ++i;
            else
                return true;
        }
    }
    return false;
}

QList<YamlLine> yamlLines(const QString &contents)
{
    // A small structural scanner, not a YAML parser. Scalar bodies and quoted
    // continuations are skipped so embedded scripts cannot become YAML keys.
    static const QRegularExpression mapping(QStringLiteral(R"re(^([ \t]*)(- +)?([A-Za-z_][A-Za-z0-9_.-]*|"[A-Za-z_][A-Za-z0-9_.-]*"|'[A-Za-z_][A-Za-z0-9_.-]*') *: *(.*)$)re"));
    static const QRegularExpression blockScalar(QStringLiteral(R"(^[|>](?:[1-9][+-]?|[+-][1-9]?|)(?:\s+#.*)?\s*$)"));
    QList<YamlLine> result;
    int blockIndent = -1;
    QChar multilineQuote;
    int lineNumber = 0;
    const auto lines = contents.split(QLatin1Char('\n'));
    for (const auto &source : lines) {
        ++lineNumber;
        QString line = source;
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        if (!multilineQuote.isNull()) {
            if (quoteCloses(line, multilineQuote, 0))
                multilineQuote = QChar();
            continue;
        }
        int spaces = 0;
        while (spaces < line.size() && line[spaces] == QLatin1Char(' '))
            ++spaces;
        if (blockIndent >= 0) {
            if (spaces > blockIndent)
                continue;
            blockIndent = -1;
        }
        if (trimmed.startsWith(QLatin1Char('#')))
            continue;
        const auto match = mapping.match(line);
        if (!match.hasMatch()) {
            // Preserve a structural boundary without pretending it is a key.
            result.append({lineNumber, spaces, spaces, false, {}, {}, false});
            continue;
        }
        const QString prefix = match.captured(1);
        const bool sequence = !match.captured(2).isEmpty();
        QString key = match.captured(3);
        if (key.startsWith(QLatin1Char('"')) || key.startsWith(QLatin1Char('\'')))
            key = key.mid(1, key.size() - 2);
        const QString value = match.captured(4).trimmed();
        const int keyIndent = prefix.size() + match.capturedLength(2);
        result.append({lineNumber, int(prefix.size()), keyIndent, sequence, key, value, prefix.contains(QLatin1Char('\t'))});
        if (blockScalar.match(value).hasMatch()) {
            blockIndent = keyIndent;
        } else if (!value.isEmpty() && (value[0] == QLatin1Char('\'') || value[0] == QLatin1Char('"'))
                   && !quoteCloses(value, value[0], 1)) {
            multilineQuote = value[0];
        }
    }
    return result;
}

bool emptyMappingValue(const QString &value)
{
    return value.isEmpty() || value.startsWith(QLatin1Char('#'));
}
}

DevOpsProfiles::Kind DevOpsProfiles::detect(const QString &filePath, const QString &contents)
{
    QString path = filePath;
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    path = path.toLower();
    const QString leaf = path.section(QLatin1Char('/'), -1);
    const bool yaml = leaf.endsWith(QStringLiteral(".yml")) || leaf.endsWith(QStringLiteral(".yaml"));
    if (!yaml && (leaf == QStringLiteral("jenkinsfile") || leaf.startsWith(QStringLiteral("jenkinsfile."))))
        return Kind::Jenkins;
    if (!yaml)
        return Kind::None;
    static const QRegularExpression workflow(QStringLiteral(R"((?:^|/)\.github/workflows/[^/]+\.ya?ml$)"));
    if (workflow.match(path).hasMatch())
        return Kind::GitHubActions;
    static const QRegularExpression azureFile(QStringLiteral(R"(^azure-pipelines(?:[.-][^/]*)?\.ya?ml$)"));
    if (azureFile.match(leaf).hasMatch())
        return Kind::AzurePipelines;
    static const QRegularExpression ansibleRole(QStringLiteral(R"((?:^|/)roles/[^/]+/(?:tasks|handlers|defaults|vars|meta)/.+\.ya?ml$)"));
    if (ansibleRole.match(path).hasMatch())
        return Kind::Ansible;

    const auto lines = yamlLines(contents.left(64 * 1024));
    bool hosts = false, playContent = false, jobs = false, trigger = false;
    bool pool = false, vmImage = false, runsOn = false, githubOn = false;
    for (const auto &line : lines) {
        hosts |= line.key == QStringLiteral("hosts") && line.keyIndent == 2;
        playContent |= (line.key == QStringLiteral("tasks") || line.key == QStringLiteral("roles")
                        || line.key == QStringLiteral("import_playbook")) && line.keyIndent == 2;
        jobs |= line.key == QStringLiteral("jobs") && line.keyIndent == 0;
        githubOn |= line.key == QStringLiteral("on") && line.keyIndent == 0;
        runsOn |= line.key == QStringLiteral("runs-on");
        trigger |= line.key == QStringLiteral("trigger") && line.keyIndent == 0;
        pool |= line.key == QStringLiteral("pool");
        vmImage |= line.key == QStringLiteral("vmImage");
    }
    if (hosts && playContent)
        return Kind::Ansible;
    if (jobs && githubOn && runsOn)
        return Kind::GitHubActions;
    if (trigger && pool && vmImage)
        return Kind::AzurePipelines;
    return Kind::None;
}

QString DevOpsProfiles::languageName(Kind kind)
{
    switch (kind) {
    case Kind::GitHubActions: return QStringLiteral("YAML (GitHub Actions)");
    case Kind::AzurePipelines: return QStringLiteral("YAML (Azure Pipelines)");
    case Kind::Ansible: return QStringLiteral("YAML (Ansible)");
    case Kind::Jenkins: return QStringLiteral("Jenkinsfile");
    default: return {};
    }
}

DevOpsProfiles::Kind DevOpsProfiles::kindForLanguage(const QString &language)
{
    for (const Kind kind : {Kind::GitHubActions, Kind::AzurePipelines, Kind::Ansible, Kind::Jenkins}) {
        if (languageName(kind) == language)
            return kind;
    }
    return Kind::None;
}

QString DevOpsProfiles::documentationUrl(Kind kind)
{
    switch (kind) {
    case Kind::GitHubActions: return QStringLiteral("https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax");
    case Kind::AzurePipelines: return QStringLiteral("https://learn.microsoft.com/en-us/azure/devops/pipelines/yaml-schema/");
    case Kind::Ansible: return QStringLiteral("https://docs.ansible.com/projects/ansible/latest/playbook_guide/playbooks_intro.html");
    case Kind::Jenkins: return QStringLiteral("https://www.jenkins.io/doc/book/pipeline/syntax/");
    default: return {};
    }
}

QList<DevOpsProfiles::Snippet> DevOpsProfiles::snippets(Kind kind)
{
    switch (kind) {
    case Kind::GitHubActions: return {
        {tr("Workflow"), QStringLiteral("name: CI\non: [push, pull_request]\npermissions:\n  contents: read\njobs:\n  build:\n    runs-on: ubuntu-latest\n    steps:\n      - name: Build\n        run: echo \"Add build commands here\"\n")},
        {tr("Run step"), QStringLiteral("- name: Run command\n  run: |\n    echo \"Add commands here\"\n")},
        {tr("Matrix job"), QStringLiteral("build:\n  strategy:\n    matrix:\n      os: [ubuntu-latest, windows-latest]\n  runs-on: ${{ matrix.os }}\n  steps:\n    - run: echo \"Add build commands here\"\n")}
    };
    case Kind::AzurePipelines: return {
        {tr("Pipeline"), QStringLiteral("trigger:\n  - main\npool:\n  vmImage: ubuntu-latest\nsteps:\n  - script: echo \"Add build commands here\"\n    displayName: Build\n")},
        {tr("PowerShell step"), QStringLiteral("- pwsh: |\n    Write-Host \"Add commands here\"\n  displayName: Run PowerShell\n")},
        {tr("Stage and job"), QStringLiteral("stages:\n  - stage: Build\n    jobs:\n      - job: BuildJob\n        steps:\n          - script: echo \"Add build commands here\"\n")}
    };
    case Kind::Ansible: return {
        {tr("Playbook"), QStringLiteral("---\n- name: Configure hosts\n  hosts: all\n  gather_facts: true\n  tasks:\n    - name: Check connectivity\n      ansible.builtin.ping:\n")},
        {tr("Debug task"), QStringLiteral("- name: Show a message\n  ansible.builtin.debug:\n    msg: \"Add a message here\"\n")},
        {tr("Block with rescue"), QStringLiteral("- name: Run guarded tasks\n  block:\n    - name: Run task\n      ansible.builtin.debug:\n        msg: \"Add a task here\"\n  rescue:\n    - name: Report failure\n      ansible.builtin.debug:\n        msg: \"Task failed\"\n")}
    };
    case Kind::Jenkins: return {
        {tr("Declarative pipeline"), QStringLiteral("pipeline {\n    agent any\n    stages {\n        stage('Build') {\n            steps {\n                echo 'Add build commands here'\n            }\n        }\n    }\n}\n")},
        {tr("Scripted pipeline"), QStringLiteral("node {\n    stage('Build') {\n        echo 'Add build commands here'\n    }\n}\n")},
        {tr("Stage"), QStringLiteral("stage('Test') {\n    steps {\n        echo 'Add test commands here'\n    }\n}\n")}
    };
    default: return {};
    }
}

QList<DevOpsProfiles::Diagnostic> DevOpsProfiles::check(Kind kind, const QString &contents)
{
    QList<Diagnostic> result;
    if (kind == Kind::None || kind == Kind::Jenkins)
        return result;
    // Explicit checks run on demand. The cap avoids freezing the editor on an
    // unusually large generated file; the caller can tell the user about it.
    if (contents.size() > 1024 * 1024) {
        result.append({1, tr("Profile checks are skipped above 1 MiB. Use a dedicated validator for this file.")});
        return result;
    }
    struct Container { int indent; QString key; };
    QList<Container> containers;
    int stepIndent = -1, stepKeyIndent = -1;
    QString stepOwner;
    QSet<QString> stepKeys;
    const QSet<QString> azureKinds = {"script", "bash", "pwsh", "powershell", "task", "checkout",
                                      "download", "downloadBuild", "getPackage", "publish", "reviewApp", "template"};
    const QSet<QString> ansibleTasks = {"tasks", "pre_tasks", "post_tasks", "handlers", "block", "rescue", "always"};
    for (const auto &line : yamlLines(contents)) {
        if (line.tabIndent)
            result.append({line.line, tr("Use spaces for YAML indentation; tabs in scalar text are allowed.")});
        while (!containers.isEmpty() && containers.last().indent >= line.keyIndent)
            containers.removeLast();
        const QString parent = containers.isEmpty() ? QString() : containers.last().key;
        if (stepIndent >= 0 && line.indent <= stepIndent) {
            stepIndent = -1;
            stepKeys.clear();
            stepOwner.clear();
        }
        const bool stepSequence = line.sequence && (parent == QStringLiteral("steps")
            || (kind == Kind::Ansible && (ansibleTasks.contains(parent) || parent.isEmpty())));
        if (stepSequence) {
            stepIndent = line.indent;
            stepKeyIndent = line.keyIndent;
            stepOwner = parent;
            stepKeys.clear();
        }
        if (stepIndent >= 0 && line.keyIndent == stepKeyIndent && parent == stepOwner && !line.key.isEmpty()) {
            if (kind == Kind::GitHubActions && parent == QStringLiteral("steps")) {
                const QString opposite = line.key == QStringLiteral("run") ? QStringLiteral("uses")
                    : line.key == QStringLiteral("uses") ? QStringLiteral("run") : QString();
                if (!opposite.isEmpty() && stepKeys.contains(opposite))
                    result.append({line.line, tr("A GitHub Actions step cannot contain both run and uses. Split it into two steps.")});
            } else if (kind == Kind::AzurePipelines && parent == QStringLiteral("steps") && azureKinds.contains(line.key)) {
                for (const auto &existing : std::as_const(stepKeys)) {
                    if (existing != line.key && azureKinds.contains(existing)) {
                        result.append({line.line, tr("This Azure Pipelines step combines %1 and %2. Use separate steps.").arg(existing, line.key)});
                        break;
                    }
                }
            } else if (kind == Kind::Ansible && line.key == QStringLiteral("when")
                       && line.value.contains(QStringLiteral("{{")) && line.value.contains(QStringLiteral("}}"))) {
                result.append({line.line, tr("Ansible when expects a Jinja expression without the surrounding {{ }} delimiters.")});
            }
            stepKeys.insert(line.key);
        }
        if (!line.key.isEmpty() && emptyMappingValue(line.value))
            containers.append({line.keyIndent, line.key});
    }
    return result;
}
