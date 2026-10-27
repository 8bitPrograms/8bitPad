// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ExternalTerminals.h"

#include <QPointer>
#include <QWidget>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSettings;
class QToolButton;

class TerminalPreferences final : public QWidget
{
    Q_OBJECT
public:
    using Discovery = std::function<QList<ExternalTerminals::Terminal>(bool refresh)>;
    explicit TerminalPreferences(QSettings *settings, QWidget *parent = nullptr,
                                 Discovery discovery = {});

public slots:
    void refreshApplications();

signals:
    void preferencesChanged();

protected:
    void changeEvent(QEvent *event) override;

private:
    void populateApplications(bool refresh);
    void saveCustomConfiguration();
    void updateAvailability();
    void refreshIcons();
    QString customDisplayName() const;

    QPointer<QSettings> settings;
    Discovery discovery;
    QList<ExternalTerminals::Terminal> applications;
    QComboBox *defaultTerminal = nullptr;
    QToolButton *refreshButton = nullptr;
    QLabel *availability = nullptr;
    QLineEdit *customName = nullptr;
    QLineEdit *customProgram = nullptr;
    QToolButton *browseButton = nullptr;
    QPlainTextEdit *customArguments = nullptr;
    QCheckBox *customNewConsole = nullptr;
};
