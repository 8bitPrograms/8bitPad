#pragma once
#include <QString>
class QMainWindow;
class QMenu;
void setupPadUpdates(QMainWindow *, QMenu *, const QString &settingsFile, bool startupCheck);
