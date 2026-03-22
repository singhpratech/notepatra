#include "preferences.h"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QRadioButton>
#include <QLabel>
#include <QPushButton>

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Preferences");
    setMinimumSize(600, 500);

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget;
    layout->addWidget(tabs);

    // General tab
    auto *general = new QWidget;
    auto *gLayout = new QVBoxLayout(general);
    auto *tbGroup = new QGroupBox("Toolbar");
    auto *tbLay = new QVBoxLayout(tbGroup);
    tbLay->addWidget(new QCheckBox("Hide toolbar"));
    gLayout->addWidget(tbGroup);
    auto *tabGroup = new QGroupBox("Tab Bar");
    auto *tabLay = new QVBoxLayout(tabGroup);
    tabLay->addWidget(new QCheckBox("Double-click to close"));
    tabLay->addWidget(new QCheckBox("Show close button on each tab"));
    gLayout->addWidget(tabGroup);
    gLayout->addStretch();
    tabs->addTab(general, "General");

    // Editing tab
    auto *editing = new QWidget;
    auto *eLay = new QVBoxLayout(editing);
    auto *caretGroup = new QGroupBox("Caret Settings");
    auto *cLay = new QHBoxLayout(caretGroup);
    cLay->addWidget(new QLabel("Width:"));
    auto *caretW = new QSpinBox; caretW->setRange(1, 3); caretW->setValue(2);
    cLay->addWidget(caretW);
    eLay->addWidget(caretGroup);
    eLay->addWidget(new QCheckBox("Highlight current line"));
    eLay->addWidget(new QCheckBox("Enable smooth font"));
    eLay->addStretch();
    tabs->addTab(editing, "Editing");

    // Margins tab
    auto *margins = new QWidget;
    auto *mLay = new QVBoxLayout(margins);
    auto *foldGroup = new QGroupBox("Fold Margin Style");
    auto *fLay = new QVBoxLayout(foldGroup);
    auto *foldCombo = new QComboBox;
    foldCombo->addItems({"Box tree", "Circle tree", "Arrow", "Simple", "None"});
    fLay->addWidget(foldCombo);
    mLay->addWidget(foldGroup);
    mLay->addWidget(new QCheckBox("Display line numbers"));
    mLay->addWidget(new QCheckBox("Display bookmark margin"));
    mLay->addStretch();
    tabs->addTab(margins, "Margins");

    // Tab Settings tab
    auto *tabSettings = new QWidget;
    auto *tsLay = new QVBoxLayout(tabSettings);
    auto *tsGroup = new QGroupBox("Tab Settings");
    auto *tsgLay = new QVBoxLayout(tsGroup);
    auto *tsRow = new QHBoxLayout;
    tsRow->addWidget(new QLabel("Tab size:"));
    auto *tabSize = new QSpinBox; tabSize->setRange(1, 16); tabSize->setValue(4);
    tsRow->addWidget(tabSize); tsRow->addStretch();
    tsgLay->addLayout(tsRow);
    tsgLay->addWidget(new QRadioButton("Replace tabs with spaces"));
    tsgLay->addWidget(new QRadioButton("Use tab character"));
    tsgLay->addWidget(new QCheckBox("Auto-indent"));
    tsLay->addWidget(tsGroup);
    tsLay->addStretch();
    tabs->addTab(tabSettings, "Tab Settings");

    // Auto-Completion tab
    auto *acTab = new QWidget;
    auto *acLay = new QVBoxLayout(acTab);
    acLay->addWidget(new QCheckBox("Enable auto-completion"));
    auto *acRow = new QHBoxLayout;
    acRow->addWidget(new QLabel("Threshold:"));
    auto *acThresh = new QSpinBox; acThresh->setRange(1, 10); acThresh->setValue(3);
    acRow->addWidget(acThresh); acRow->addStretch();
    acLay->addLayout(acRow);
    acLay->addStretch();
    tabs->addTab(acTab, "Auto-Completion");

    // New Document tab
    auto *newDoc = new QWidget;
    auto *ndLay = new QVBoxLayout(newDoc);
    auto *eolGroup = new QGroupBox("Line ending");
    auto *eolLay = new QVBoxLayout(eolGroup);
    eolLay->addWidget(new QRadioButton("Windows (CR LF)"));
    auto *lf = new QRadioButton("Unix (LF)"); lf->setChecked(true);
    eolLay->addWidget(lf);
    eolLay->addWidget(new QRadioButton("Macintosh (CR)"));
    ndLay->addWidget(eolGroup);
    ndLay->addStretch();
    tabs->addTab(newDoc, "New Document");

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto *closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);
}
