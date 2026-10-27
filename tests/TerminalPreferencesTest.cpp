// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerminalPreferences.h"
#include "ExternalTerminals.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

namespace {
QList<ExternalTerminals::Terminal> testApplications()
{
    // These are invented paths. Neither discovery injection nor the widget
    // may execute them; launch belongs to the explicit Open Shell action.
    return {
        {QStringLiteral("alpha"), QStringLiteral("Alpha Terminal"), QStringLiteral("/missing/alpha-terminal"), {}},
        {QStringLiteral("beta"), QStringLiteral("Beta Terminal"), QStringLiteral("/missing/beta-terminal"), {}}
    };
}

QMap<QString, QVariant> storedValues(QSettings &settings)
{
    QMap<QString, QVariant> result;
    for (const auto &key : settings.allKeys())
        result.insert(key, settings.value(key));
    return result;
}
}

class TerminalPreferencesTest : public QObject
{
    Q_OBJECT
private slots:
    void openingDoesNotWriteSettings();
    void defaultSelectionSavesImmediately();
    void refreshKeepsMissingSelectionAndCustomValues();
    void customArgumentsRemainLiteral();
    void newConsoleChoicePersists();
    void emptyDiscoveryHasActionableState();
    void customEditsDoNotResetAnotherDefault();
};

void TerminalPreferencesTest::openingDoesNotWriteSettings()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(directory.filePath(QStringLiteral("terminal.ini")), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Unrelated/Keep"), QStringLiteral("untouched"));
    const auto before = storedValues(settings);
    QList<bool> discoveryCalls;
    TerminalPreferences widget(&settings, nullptr, [&discoveryCalls](bool refresh) {
        discoveryCalls.append(refresh);
        return testApplications();
    });
    QCOMPARE(discoveryCalls, QList<bool>({false}));
    QCOMPARE(storedValues(settings), before);
    QCOMPARE(widget.findChild<QComboBox *>(QStringLiteral("terminalDefaultCombo"))->currentData().toString(), QStringLiteral("auto"));
    QVERIFY(widget.findChild<QLineEdit *>(QStringLiteral("terminalCustomProgram"))->text().isEmpty());
}

void TerminalPreferencesTest::defaultSelectionSavesImmediately()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath(QStringLiteral("terminal.ini")), QSettings::IniFormat);
    TerminalPreferences widget(&settings, nullptr, [](bool) { return testApplications(); });
    auto *combo = widget.findChild<QComboBox *>(QStringLiteral("terminalDefaultCombo"));
    QVERIFY(combo);
    QSignalSpy changed(&widget, &TerminalPreferences::preferencesChanged);
    combo->setCurrentIndex(combo->findData(QStringLiteral("beta")));
    QCOMPARE(ExternalTerminals::readSettings(&settings).terminalId, QStringLiteral("beta"));
    QCOMPARE(changed.count(), 1);
    combo->setCurrentIndex(combo->findData(QStringLiteral("custom")));
    QCOMPARE(ExternalTerminals::readSettings(&settings).terminalId, QStringLiteral("custom"));
    const auto *message = widget.findChild<QLabel *>(QStringLiteral("terminalAvailabilityLabel"));
    QVERIFY(message);
    QVERIFY(message->text().contains(QStringLiteral("Choose an executable")));
    combo->setCurrentIndex(combo->findData(QStringLiteral("auto")));
    QCOMPARE(ExternalTerminals::readSettings(&settings).terminalId, QStringLiteral("auto"));
}

void TerminalPreferencesTest::refreshKeepsMissingSelectionAndCustomValues()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath(QStringLiteral("terminal.ini")), QSettings::IniFormat);
    ExternalTerminals::Settings saved;
    saved.terminalId = QStringLiteral("temporarily-missing");
    saved.customName = QStringLiteral("My custom shell");
    saved.customProgram = QStringLiteral("C:/Tools/My Shell/shell.exe");
    saved.customArguments = {QStringLiteral("two words"), QStringLiteral("--folder={folder}"), QStringLiteral("quoted \"value\"")};
    saved.customNewConsole = true;
    ExternalTerminals::writeSettings(&settings, saved);
    const auto before = storedValues(settings);
    QList<bool> discoveryCalls;
    TerminalPreferences widget(&settings, nullptr, [&discoveryCalls](bool refresh) {
        discoveryCalls.append(refresh);
        return refresh ? QList<ExternalTerminals::Terminal>() : testApplications();
    });
    auto *combo = widget.findChild<QComboBox *>(QStringLiteral("terminalDefaultCombo"));
    QVERIFY(combo);
    QCOMPARE(combo->currentData().toString(), saved.terminalId);
    QVERIFY(combo->currentText().contains(QStringLiteral("Unavailable")));
    auto *arguments = widget.findChild<QPlainTextEdit *>(QStringLiteral("terminalCustomArguments"));
    const QString argumentText = arguments->toPlainText();
    QSignalSpy changed(&widget, &TerminalPreferences::preferencesChanged);
    auto *refresh = widget.findChild<QToolButton *>(QStringLiteral("terminalRefreshButton"));
    QVERIFY(refresh);
    refresh->click();
    QCOMPARE(discoveryCalls, QList<bool>({false, true}));
    QCOMPARE(combo->currentData().toString(), saved.terminalId);
    QCOMPARE(arguments->toPlainText(), argumentText);
    QCOMPARE(widget.findChild<QLineEdit *>(QStringLiteral("terminalCustomName"))->text(), saved.customName);
    QCOMPARE(widget.findChild<QLineEdit *>(QStringLiteral("terminalCustomProgram"))->text(), saved.customProgram);
    QCOMPARE(storedValues(settings), before);
    QCOMPARE(changed.count(), 0);
}

void TerminalPreferencesTest::customArgumentsRemainLiteral()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath(QStringLiteral("terminal.ini")), QSettings::IniFormat);
    TerminalPreferences widget(&settings, nullptr, [](bool) { return testApplications(); });
    auto *arguments = widget.findChild<QPlainTextEdit *>(QStringLiteral("terminalCustomArguments"));
    QVERIFY(arguments);
    const QString text = QString::fromUtf8("two words\n\"quoted\"\nsemi;colon\n$(literal)\n{folder}\nTürkçe ıİ 🙂\n  spaces stay  \n\n");
    arguments->setPlainText(text);
    const QStringList expected = {QStringLiteral("two words"), QStringLiteral("\"quoted\""), QStringLiteral("semi;colon"),
        QStringLiteral("$(literal)"), QStringLiteral("{folder}"), QString::fromUtf8("Türkçe ıİ 🙂"), QStringLiteral("  spaces stay  ")};
    QCOMPARE(ExternalTerminals::readSettings(&settings).customArguments, expected);
    QCOMPARE(arguments->toPlainText(), text);
    widget.refreshApplications();
    QCOMPARE(arguments->toPlainText(), text);
    QCOMPARE(ExternalTerminals::readSettings(&settings).customArguments, expected);
}

void TerminalPreferencesTest::newConsoleChoicePersists()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath(QStringLiteral("terminal.ini")), QSettings::IniFormat);
    {
        TerminalPreferences widget(&settings, nullptr, [](bool) { return testApplications(); });
        auto *console = widget.findChild<QCheckBox *>(QStringLiteral("terminalCustomNewConsole"));
        QVERIFY(console);
        console->setChecked(true);
        QVERIFY(ExternalTerminals::readSettings(&settings).customNewConsole);
        widget.findChild<QLineEdit *>(QStringLiteral("terminalCustomName"))->setText(QStringLiteral("Work shell"));
        widget.findChild<QLineEdit *>(QStringLiteral("terminalCustomProgram"))->setText(QStringLiteral("work-shell"));
        QCOMPARE(ExternalTerminals::readSettings(&settings).customName, QStringLiteral("Work shell"));
        QCOMPARE(ExternalTerminals::readSettings(&settings).customProgram, QStringLiteral("work-shell"));
    }
    TerminalPreferences reopened(&settings, nullptr, [](bool) { return testApplications(); });
    QVERIFY(reopened.findChild<QCheckBox *>(QStringLiteral("terminalCustomNewConsole"))->isChecked());
    QCOMPARE(reopened.findChild<QLineEdit *>(QStringLiteral("terminalCustomName"))->text(), QStringLiteral("Work shell"));
}

void TerminalPreferencesTest::emptyDiscoveryHasActionableState()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath(QStringLiteral("terminal.ini")), QSettings::IniFormat);
    TerminalPreferences widget(&settings, nullptr, [](bool) { return QList<ExternalTerminals::Terminal>(); });
    const auto *combo = widget.findChild<QComboBox *>(QStringLiteral("terminalDefaultCombo"));
    QCOMPARE(combo->count(), 2); // automatic + custom remain usable
    const auto *message = widget.findChild<QLabel *>(QStringLiteral("terminalAvailabilityLabel"));
    QVERIFY(message->text().contains(QStringLiteral("No terminal application was found")));
    QVERIFY(widget.findChild<QLineEdit *>(QStringLiteral("terminalCustomProgram"))->isEnabled());
    QVERIFY(widget.findChild<QToolButton *>(QStringLiteral("terminalBrowseButton"))->isEnabled());
}

void TerminalPreferencesTest::customEditsDoNotResetAnotherDefault()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath(QStringLiteral("terminal.ini")), QSettings::IniFormat);
    TerminalPreferences widget(&settings, nullptr, [](bool) { return testApplications(); });
    auto changedElsewhere = ExternalTerminals::readSettings(&settings);
    changedElsewhere.terminalId = QStringLiteral("beta");
    ExternalTerminals::writeSettings(&settings, changedElsewhere);
    widget.findChild<QLineEdit *>(QStringLiteral("terminalCustomName"))->setText(QStringLiteral("Updated label"));
    QCOMPARE(ExternalTerminals::readSettings(&settings).terminalId, QStringLiteral("beta"));
    widget.refreshApplications();
    QCOMPARE(widget.findChild<QComboBox *>(QStringLiteral("terminalDefaultCombo"))->currentData().toString(), QStringLiteral("beta"));
}

QTEST_MAIN(TerminalPreferencesTest)
#include "TerminalPreferencesTest.moc"
