// SPDX-License-Identifier: GPL-3.0-or-later
#include "DevOpsProfiles.h"
#include "LanguageMenu.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using DevOpsProfiles::Kind;

class LanguageProfilesTest : public QObject
{
    Q_OBJECT
private slots:
    void taxonomyPreservesEveryLanguage();
    void favoritesStartEmptyAndPersist();
    void rebuildKeepsSelectionAndUtilityActions();
    void rightClickAddsAndRemovesFavorite();
    void keyboardFavoriteMenu();
    void detectProfile_data();
    void detectProfile();
    void profileRoundTrips();
    void githubStepChecks();
    void azureStepChecks();
    void ansibleConditionChecks();
    void yamlScalarBodiesAreNotConfiguration();
    void checksHaveExplicitLimits();
};

void LanguageProfilesTest::taxonomyPreservesEveryLanguage()
{
    const QStringList available = {"Python", "C", "C++", "C#", "Rust", "Ruby", "bash", "Markdown", "Go",
        "HTML", "Haskell", "Java", "JavaScript", "JSON", "Lua", "PowerShell", "Text", "YAML", "XML",
        "SQL", "SQL (Oracle)", "SQL (PostgreSQL)", "SQL (MSSQL)", "Jenkinsfile", "Groovy", "YAML (Ansible)",
        "YAML (Azure Pipelines)", "YAML (GitHub Actions)", "Zebra extension", "ADA", "Abaqus"};
    const auto groups = LanguageMenu::groups(available);
    QSet<QString> seen;
    for (const auto &group : groups) {
        QVERIFY(!group.title.isEmpty());
        for (const auto &language : group.languages) {
            QVERIFY2(!seen.contains(language), qPrintable(language));
            seen.insert(language);
        }
    }
    QCOMPARE(seen, QSet<QString>(available.cbegin(), available.cend()));
    QCOMPARE(groups.last().title, QStringLiteral("Other"));
    QCOMPARE(groups.last().languages, QStringList({"Abaqus", "ADA", "Zebra extension"}));
    QVERIFY(LanguageMenu::groups({}).isEmpty());
    QCOMPARE(LanguageMenu::groups({"Python", "Python", ""}).first().languages, QStringList({"Python"}));
}

void LanguageProfilesTest::favoritesStartEmptyAndPersist()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.ini"));
    {
        QSettings settings(path, QSettings::IniFormat);
        QMenu menu;
        QActionGroup group(&menu);
        LanguageMenu helper(&menu, &group, &settings);
        helper.setLanguages({"Python", "C++", "SQL (MSSQL)"});
        QVERIFY(helper.favorites().isEmpty());
        const auto favorites = menu.findChild<QMenu *>(QStringLiteral("languageFavorites"));
        QVERIFY(favorites);
        QVERIFY(!favorites->menuAction()->isVisible());
        for (QAction *action : menu.actions()) {
            if (action->isVisible() && !action->isSeparator())
                QVERIFY(action->menu()); // no pre-pinned common languages
        }
        helper.setFavorite(QStringLiteral("Python"), true);
        helper.setFavorite(QStringLiteral("SQL (MSSQL)"), true);
        helper.setFavorite(QStringLiteral("Not installed"), true);
        QCOMPARE(helper.favorites().size(), 2);
        QVERIFY(favorites->menuAction()->isVisible());
        QCOMPARE(favorites->actions().first(), helper.actionForLanguage(QStringLiteral("Python")));
        QSignalSpy triggered(&helper, &LanguageMenu::languageTriggered);
        helper.actionForLanguage(QStringLiteral("Python"))->trigger();
        QCOMPARE(triggered.count(), 1);
        QCOMPARE(triggered.first().first().toString(), QStringLiteral("Python"));
        QVERIFY(favorites->actions().first()->isChecked());
    }
    {
        QSettings settings(path, QSettings::IniFormat);
        QMenu menu;
        QActionGroup group(&menu);
        LanguageMenu helper(&menu, &group, &settings);
        helper.setLanguages({"Python", "C++", "SQL (MSSQL)"});
        QCOMPARE(helper.favorites(), QStringList({"Python", "SQL (MSSQL)"}));
        helper.setFavorite(QStringLiteral("Python"), false);
        helper.setLanguages({"Python", "C++", "SQL (MSSQL)"});
        QCOMPARE(group.actions().size(), 3);
        QCOMPARE(helper.favorites(), QStringList({"SQL (MSSQL)"}));
    }
}

void LanguageProfilesTest::rightClickAddsAndRemovesFavorite()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    QMenu menu;
    QActionGroup group(&menu);
    LanguageMenu helper(&menu, &group, &settings);
    helper.setLanguages({"Python", "Ruby"});
    QSignalSpy languageTriggered(&helper, &LanguageMenu::languageTriggered);
    QMenu *scripting = nullptr;
    for (QAction *action : menu.actions()) {
        if (action->menu() && action->menu()->actions().contains(helper.actionForLanguage(QStringLiteral("Python"))))
            scripting = action->menu();
    }
    QVERIFY(scripting);
    scripting->popup(QPoint(100, 100));
    QTRY_VERIFY(scripting->isVisible());
    QTest::mouseClick(scripting, Qt::RightButton, Qt::NoModifier,
                      scripting->actionGeometry(helper.actionForLanguage(QStringLiteral("Python"))).center());
    auto context = scripting->findChild<QMenu *>(QStringLiteral("languageFavoriteContextMenu"));
    QVERIFY(context);
    QTRY_VERIFY(context->isVisible());
    QCOMPARE(context->actions().first()->text(), QStringLiteral("Add to Favorites"));
    QTest::mouseClick(context, Qt::LeftButton, Qt::NoModifier, context->actionGeometry(context->actions().first()).center());
    QTRY_VERIFY(helper.isFavorite(QStringLiteral("Python")));
    QCOMPARE(languageTriggered.count(), 0); // right-click must not switch the editor's lexer

    auto favorites = menu.findChild<QMenu *>(QStringLiteral("languageFavorites"));
    QVERIFY(favorites);
    favorites->popup(QPoint(200, 100));
    QTRY_VERIFY(favorites->isVisible());
    QTest::mouseClick(favorites, Qt::RightButton, Qt::NoModifier, favorites->actionGeometry(favorites->actions().first()).center());
    context = favorites->findChild<QMenu *>(QStringLiteral("languageFavoriteContextMenu"));
    QVERIFY(context);
    QTRY_VERIFY(context->isVisible());
    QCOMPARE(context->actions().first()->text(), QStringLiteral("Remove from Favorites"));
    QTest::mouseClick(context, Qt::LeftButton, Qt::NoModifier, context->actionGeometry(context->actions().first()).center());
    QTRY_VERIFY(!helper.isFavorite(QStringLiteral("Python")));
    QCOMPARE(languageTriggered.count(), 0);
    QVERIFY(!favorites->menuAction()->isVisible());
}

void LanguageProfilesTest::rebuildKeepsSelectionAndUtilityActions()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    QMenu menu;
    QActionGroup group(&menu);
    LanguageMenu helper(&menu, &group, &settings);
    helper.setLanguages({"Python", "C++"});
    helper.actionForLanguage(QStringLiteral("Python"))->setChecked(true);
    helper.setFavorite(QStringLiteral("Python"), true);
    QAction *utility = menu.addAction(QStringLiteral("DevOps assistance"));
    QSignalSpy triggered(&helper, &LanguageMenu::languageTriggered);
    helper.setLanguages({"Python", "C++", "Rust"});
    QCOMPARE(group.actions().size(), 3);
    QCOMPARE(group.checkedAction(), helper.actionForLanguage(QStringLiteral("Python")));
    QCOMPARE(menu.actions().last(), utility);
    QCOMPARE(triggered.count(), 0);
    auto favorites = menu.findChild<QMenu *>(QStringLiteral("languageFavorites"));
    QVERIFY(favorites);
    QCOMPARE(favorites->actions().first(), group.checkedAction());
}

void LanguageProfilesTest::keyboardFavoriteMenu()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    QMenu menu;
    QActionGroup group(&menu);
    LanguageMenu helper(&menu, &group, &settings);
    helper.setLanguages({"Python"});
    QMenu *submenu = menu.actions().last()->menu();
    QVERIFY(submenu);
    submenu->popup(QPoint(100, 100));
    submenu->setActiveAction(helper.actionForLanguage(QStringLiteral("Python")));
    QTest::keyClick(submenu, Qt::Key_F10, Qt::ShiftModifier);
    auto context = submenu->findChild<QMenu *>(QStringLiteral("languageFavoriteContextMenu"));
    QVERIFY(context);
    context->setActiveAction(context->actions().first());
    QTest::keyClick(context, Qt::Key_Return);
    QTRY_VERIFY(helper.isFavorite(QStringLiteral("Python")));
}

void LanguageProfilesTest::detectProfile_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("contents");
    QTest::addColumn<int>("expected");
    QTest::newRow("github-windows") << QStringLiteral("C:\\repo\\.github\\workflows\\ci.yml") << QString() << int(Kind::GitHubActions);
    QTest::newRow("github-linux") << QStringLiteral("/repo/.github/workflows/build.yaml") << QString() << int(Kind::GitHubActions);
    QTest::newRow("nested-not-workflow") << QStringLiteral("/repo/.github/workflows/nested/config.yml") << QString() << int(Kind::None);
    QTest::newRow("azure-default") << QStringLiteral("azure-pipelines.yml") << QString() << int(Kind::AzurePipelines);
    QTest::newRow("azure-variant") << QStringLiteral("ci/azure-pipelines-release.yaml") << QString() << int(Kind::AzurePipelines);
    QTest::newRow("role-task") << QStringLiteral("roles/web/tasks/main.yaml") << QString() << int(Kind::Ansible);
    QTest::newRow("role-defaults") << QStringLiteral("/srv/roles/web/defaults/main.yml") << QString() << int(Kind::Ansible);
    QTest::newRow("ordinary-task-folder") << QStringLiteral("/repo/tasks/main.yml") << QString() << int(Kind::None);
    QTest::newRow("jenkins") << QStringLiteral("/repo/Jenkinsfile") << QString() << int(Kind::Jenkins);
    QTest::newRow("jenkins-variant") << QStringLiteral("/repo/Jenkinsfile.release") << QString() << int(Kind::Jenkins);
    QTest::newRow("jenkins-yaml-is-not-groovy") << QStringLiteral("/repo/Jenkinsfile.yaml") << QString() << int(Kind::None);
    QTest::newRow("groovy-is-not-always-jenkins") << QStringLiteral("tools/build.groovy") << QStringLiteral("println 'hello'") << int(Kind::None);
    QTest::newRow("ansible-content") << QStringLiteral("deploy.yml") << QStringLiteral("---\n- name: Deploy\n  hosts: all\n  tasks:\n    - ansible.builtin.ping:\n") << int(Kind::Ansible);
    QTest::newRow("github-content") << QStringLiteral("ci.yml") << QStringLiteral("on: push\njobs:\n  test:\n    runs-on: ubuntu-latest\n") << int(Kind::GitHubActions);
    QTest::newRow("azure-content") << QStringLiteral("ci.yml") << QStringLiteral("trigger: [main]\npool:\n  vmImage: ubuntu-latest\n") << int(Kind::AzurePipelines);
    QTest::newRow("wrong-extension") << QStringLiteral("notes.txt") << QStringLiteral("trigger: [main]\npool:\n  vmImage: ubuntu-latest\n") << int(Kind::None);
    QTest::newRow("scalar-not-profile") << QStringLiteral("notes.yaml") << QStringLiteral("example: |\n  - hosts: all\n    tasks:\n      - ansible.builtin.ping:\n") << int(Kind::None);
    QTest::newRow("ordinary-yaml") << QStringLiteral("settings.yaml") << QStringLiteral("name: app\nversion: 1\n") << int(Kind::None);
}

void LanguageProfilesTest::detectProfile()
{
    QFETCH(QString, path);
    QFETCH(QString, contents);
    QFETCH(int, expected);
    QCOMPARE(int(DevOpsProfiles::detect(path, contents)), expected);
}

void LanguageProfilesTest::profileRoundTrips()
{
    for (Kind kind : {Kind::GitHubActions, Kind::AzurePipelines, Kind::Ansible, Kind::Jenkins}) {
        QCOMPARE(DevOpsProfiles::kindForLanguage(DevOpsProfiles::languageName(kind)), kind);
        QVERIFY(DevOpsProfiles::documentationUrl(kind).startsWith(QStringLiteral("https://")));
        const auto snippets = DevOpsProfiles::snippets(kind);
        QVERIFY(!snippets.isEmpty());
        for (const auto &snippet : snippets) {
            QVERIFY(!snippet.title.isEmpty());
            QVERIFY(!snippet.text.isEmpty());
            QVERIFY2(DevOpsProfiles::check(kind, snippet.text).isEmpty(), qPrintable(snippet.title));
        }
    }
    QCOMPARE(DevOpsProfiles::kindForLanguage(QStringLiteral("YAML")), Kind::None);
    QVERIFY(DevOpsProfiles::snippets(Kind::None).isEmpty());
}

void LanguageProfilesTest::githubStepChecks()
{
    const auto bad = DevOpsProfiles::check(Kind::GitHubActions,
        QStringLiteral("jobs:\n  build:\n    steps:\n      - name: Build\n        run: echo hello\n        uses: example/action@v1\n"));
    QCOMPARE(bad.size(), 1);
    QCOMPARE(bad.first().line, 6);
    QVERIFY(bad.first().message.contains(QStringLiteral("run and uses")));
    const auto quotedKeys = DevOpsProfiles::check(Kind::GitHubActions,
        QStringLiteral("\"steps\":\n  - 'run': echo hello\n    \"uses\": example/action@v1\n"));
    QCOMPARE(quotedKeys.size(), 1);
    QCOMPARE(quotedKeys.first().line, 3);
    QVERIFY(DevOpsProfiles::check(Kind::GitHubActions,
        QStringLiteral("jobs:\n  build:\n    steps:\n      - run: echo hello\n      - uses: example/action@v1\n        with:\n          run: value\n")).isEmpty());
    QVERIFY(DevOpsProfiles::check(Kind::GitHubActions,
        QStringLiteral("steps:\n- run: echo hello\n- uses: example/action@v1\n")).isEmpty());
}

void LanguageProfilesTest::azureStepChecks()
{
    const auto bad = DevOpsProfiles::check(Kind::AzurePipelines,
        QStringLiteral("steps:\n  - script: echo hello\n    task: CmdLine@2\n"));
    QCOMPARE(bad.size(), 1);
    QCOMPARE(bad.first().line, 3);
    QVERIFY(DevOpsProfiles::check(Kind::AzurePipelines,
        QStringLiteral("steps:\n  - task: CmdLine@2\n    inputs:\n      script: echo hello\n  - pwsh: Write-Host hello\n")).isEmpty());
    QVERIFY(DevOpsProfiles::check(Kind::AzurePipelines,
        QStringLiteral("steps:\n  - template: steps.yml\n    parameters:\n      script: hello\n      task: example\n")).isEmpty());
}

void LanguageProfilesTest::ansibleConditionChecks()
{
    const auto bad = DevOpsProfiles::check(Kind::Ansible,
        QStringLiteral("- name: Example\n  ansible.builtin.debug:\n    msg: hello\n  when: \"{{ enabled }}\"\n"));
    QCOMPARE(bad.size(), 1);
    QCOMPARE(bad.first().line, 4);
    QVERIFY(DevOpsProfiles::check(Kind::Ansible,
        QStringLiteral("- name: Example\n  ansible.builtin.debug:\n    msg: hello\n  when: enabled | bool\n")).isEmpty());
    QVERIFY(DevOpsProfiles::check(Kind::Ansible,
        QStringLiteral("- name: Example\n  vars:\n    when: \"{{ label }}\"\n  tasks: []\n")).isEmpty());
}

void LanguageProfilesTest::yamlScalarBodiesAreNotConfiguration()
{
    QVERIFY(DevOpsProfiles::check(Kind::GitHubActions,
        QStringLiteral("steps:\n  - run: |\n      uses: inside a shell string\n      \ttext with a tab\n")).isEmpty());
    QVERIFY(DevOpsProfiles::check(Kind::GitHubActions,
        QStringLiteral("steps:\n  - run: \"multiline\n    uses: still quoted\n    end\"\n")).isEmpty());
    const auto tab = DevOpsProfiles::check(Kind::AzurePipelines, QStringLiteral("steps:\n\t- script: echo hello\n"));
    QCOMPARE(tab.size(), 1);
    QCOMPARE(tab.first().line, 2);
}

void LanguageProfilesTest::checksHaveExplicitLimits()
{
    const QString large(1024 * 1024 + 1, QLatin1Char('x'));
    const auto result = DevOpsProfiles::check(Kind::Ansible, large);
    QCOMPARE(result.size(), 1);
    QVERIFY(result.first().message.contains(QStringLiteral("skipped")));
    QVERIFY(DevOpsProfiles::check(Kind::Jenkins, QStringLiteral("not valid Groovy {" )).isEmpty());
    QVERIFY(DevOpsProfiles::check(Kind::None, QStringLiteral("\tanything: value")).isEmpty());
}

QTEST_MAIN(LanguageProfilesTest)
#include "LanguageProfilesTest.moc"
