#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <QDialog>

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);
};

#endif
