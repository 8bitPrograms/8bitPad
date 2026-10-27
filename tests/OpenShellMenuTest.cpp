// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenShellMenu.h"
#include <QMenu>
#include <QTemporaryDir>
#include <QtTest>

class OpenShellMenuTest : public QObject
{
    Q_OBJECT
private slots:
    void explicitChoicePassesDirectoryWithoutChangingDefault()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString target = folder.filePath(QString::fromUtf8("Türkçe & $ folder; [one]"));
        QVERIFY(QDir().mkdir(target));
        const QList<ExternalTerminals::Terminal> terminals = {
            {"one", "First terminal", "first.exe", {}}, {"two", "Second terminal", "second.exe", {}}};
        ExternalTerminals::Settings settings;
        settings.terminalId = "one";
        QMenu menu;
        QString selected, directory;
        int requests = 0;
        populateOpenShellMenu(&menu, target, settings, terminals,
            [&](const auto &terminal, const auto &path) { selected=terminal.id; directory=path; ++requests; }, [] {});
        QAction *second = nullptr;
        for (auto *action : menu.actions()) if (action->data().toString() == "two") second = action;
        QVERIFY(second);
        second->trigger();
        QCOMPARE(selected, QString("two"));
        QCOMPARE(directory, target);
        QCOMPARE(requests, 1);
        QCOMPARE(settings.terminalId, QString("one"));
    }

    void defaultAndCustomPreserveDistinctChoices()
    {
        QTemporaryDir folder;
        ExternalTerminals::Settings settings;
        settings.terminalId = "custom";
        settings.customName = "My terminal";
        settings.customProgram = "/custom/application";
        settings.customArguments = {"--directory", "{folder}"};
        QMenu menu;
        ExternalTerminals::Terminal selected;
        populateOpenShellMenu(&menu, folder.path(), settings, {},
            [&](const auto &terminal, const auto &) { selected=terminal; }, [] {});
        auto *action = menu.findChild<QAction *>("openShellDefault");
        QVERIFY(action);
        QVERIFY(action->isEnabled());
        action->trigger();
        QCOMPARE(selected.id, QString("custom"));
        QCOMPARE(selected.name, QString("My terminal"));
        QCOMPARE(selected.arguments, settings.customArguments);
    }

    void missingDefaultOffersConfiguration()
    {
        QTemporaryDir folder;
        ExternalTerminals::Settings settings;
        settings.terminalId = "removed-terminal";
        QMenu menu;
        int requests = 0, configured = 0;
        populateOpenShellMenu(&menu, folder.path(), settings, {},
            [&](const auto &, const auto &) { ++requests; }, [&] { ++configured; });
        auto *action = menu.findChild<QAction *>("openShellDefault");
        QVERIFY(action && !action->isEnabled());
        auto *configure = menu.findChild<QAction *>("configureTerminals");
        QVERIFY(configure && configure->isEnabled());
        configure->trigger();
        QCOMPARE(configured, 1);
        QCOMPARE(requests, 0);
    }

    void nonexistentFolderDisablesLaunchChoices()
    {
        QTemporaryDir folder;
        QMenu menu;
        populateOpenShellMenu(&menu, folder.filePath("missing"), {}, {{"one", "Terminal", "terminal", {}}},
            [](const auto &, const auto &) { QFAIL("Unexpected launch"); }, [] {});
        for (auto *action : menu.actions())
            if (action->objectName().startsWith("openShell")) QVERIFY(!action->isEnabled());
        QVERIFY(menu.findChild<QAction *>("configureTerminals")->isEnabled());
    }
};
QTEST_MAIN(OpenShellMenuTest)
#include "OpenShellMenuTest.moc"
