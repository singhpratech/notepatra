// SPDX-License-Identifier: GPL-3.0-or-later

#include "functionlist.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QRegularExpression>
#include <QTreeWidgetItem>

FunctionList::FunctionList(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QLabel("  Function List");
    header->setMinimumHeight(28);
    header->setStyleSheet("QLabel { font-weight: 600; padding: 4px 6px; }");
    layout->addWidget(header);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setAnimated(true);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        QVariant v = item->data(0, Qt::UserRole);
        if (v.isValid())
            emit navigateRequested(v.toInt());
    });
}

void FunctionList::updateSymbols(const QString &text, const QString &language) {
    m_tree->clear();

    struct Pattern { QRegularExpression re; QString kind; };
    QVector<Pattern> patterns;

    if (language == "Python") {
        patterns.append({QRegularExpression("^\\s*class\\s+(\\w+)"), "class"});
        patterns.append({QRegularExpression("^\\s*(?:async\\s+)?def\\s+(\\w+)"), "function"});
    } else if (language == "JavaScript" || language == "TypeScript") {
        patterns.append({QRegularExpression("^\\s*class\\s+(\\w+)"), "class"});
        patterns.append({QRegularExpression("^\\s*(?:async\\s+)?function\\s+(\\w+)"), "function"});
        patterns.append({QRegularExpression("^\\s*(?:const|let|var)\\s+(\\w+)\\s*=\\s*(?:async\\s+)?\\("), "function"});
    } else if (language == "C" || language == "C++" || language == "C#" || language == "Java") {
        patterns.append({QRegularExpression("^\\s*(?:public|private|protected)?\\s*class\\s+(\\w+)"), "class"});
        patterns.append({QRegularExpression("^\\s*(?:\\w+\\s+)+(\\w+)\\s*\\([^)]*\\)\\s*\\{"), "function"});
    } else {
        patterns.append({QRegularExpression("^\\s*(?:class|struct|interface)\\s+(\\w+)"), "class"});
        patterns.append({QRegularExpression("^\\s*(?:def|function|fn|func|sub)\\s+(\\w+)"), "function"});
    }

    QTreeWidgetItem *classesNode = nullptr, *funcsNode = nullptr;
    QStringList lines = text.split('\n');

    for (int lineNum = 0; lineNum < lines.size(); lineNum++) {
        const QString &line = lines[lineNum];
        for (const auto &pat : patterns) {
            auto match = pat.re.match(line);
            if (match.hasMatch()) {
                QString name = match.captured(1);
                QTreeWidgetItem *parent;
                if (pat.kind == "class") {
                    if (!classesNode) {
                        classesNode = new QTreeWidgetItem(m_tree, QStringList("Classes"));
                        classesNode->setExpanded(true);
                        QFont bf = classesNode->font(0); bf.setBold(true); classesNode->setFont(0, bf);
                    }
                    parent = classesNode;
                } else {
                    if (!funcsNode) {
                        funcsNode = new QTreeWidgetItem(m_tree, QStringList("Functions"));
                        funcsNode->setExpanded(true);
                        QFont bf = funcsNode->font(0); bf.setBold(true); funcsNode->setFont(0, bf);
                    }
                    parent = funcsNode;
                }
                auto *item = new QTreeWidgetItem(parent, QStringList(name));
                item->setData(0, Qt::UserRole, lineNum + 1);
                break;
            }
        }
    }
}
