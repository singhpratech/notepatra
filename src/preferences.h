#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <QDialog>
#include <functional>

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

private:
    // Deferred save callback — filled in when the AI tab is constructed so
    // the Close button can flush AI backend settings to Config without the
    // dialog having to know the widget layout details.
    std::function<void()> m_saveAiSettings;
};

#endif
