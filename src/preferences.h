// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <QDialog>

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

signals:
    // v0.1.42 — emitted when the user clicks OK or Apply. MainWindow
    // listens and re-applies Config to every open editor + propagates
    // to chrome (toolbar visibility, tab bar settings, etc.).
    void settingsApplied();
};

#endif
