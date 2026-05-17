// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef OLLAMASTATUS_H
#define OLLAMASTATUS_H

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QHBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

/**
 * Ollama status bar widget — shows green/red dot, model name, model selector.
 * Reusable in any panel that uses Ollama.
 */
class OllamaStatus : public QWidget {
    Q_OBJECT
public:
    explicit OllamaStatus(QWidget *parent = nullptr) : QWidget(parent) {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(4, 2, 4, 2);
        layout->setSpacing(6);

        m_statusDot = new QLabel;
        m_statusDot->setFixedSize(12, 12);
        layout->addWidget(m_statusDot);

        m_statusLabel = new QLabel("Checking Ollama...");
        m_statusLabel->setStyleSheet("font-size: 11px;");
        layout->addWidget(m_statusLabel);

        layout->addWidget(new QLabel("Model:"));
        m_modelCombo = new QComboBox;
        m_modelCombo->setEditable(true);
        // 150 was tight on Windows where "(not connected)" rendered as
        // "ot connected" because dropdown chrome ate into the visible
        // text area. 200 + AdjustToContents lets it size up for longer
        // model names like "llama3.1:70b-instruct-q4_K_M" too.
        m_modelCombo->setMinimumWidth(200);
        m_modelCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        layout->addWidget(m_modelCombo);

        layout->addStretch();

        m_nam = new QNetworkAccessManager(this);
        checkStatus();
    }

    QString selectedModel() const { return m_modelCombo->currentText(); }
    bool isAvailable() const { return m_available; }

    void checkStatus() {
        QNetworkRequest req(QUrl("http://localhost:11434/api/tags"));
        auto *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            if (reply->error() == QNetworkReply::NoError) {
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                QJsonArray models = doc.object()["models"].toArray();

                m_available = true;
                m_statusDot->setStyleSheet("background: #4CAF50; border-radius: 6px;");
                m_statusLabel->setText(QString("Ollama running (%1 model%2)")
                                       .arg(models.size()).arg(models.size() != 1 ? "s" : ""));
                m_statusLabel->setStyleSheet("font-size: 11px; color: #2E7D32;");

                m_modelCombo->clear();
                for (const auto &m : models) {
                    m_modelCombo->addItem(m.toObject()["name"].toString());
                }
            } else {
                m_available = false;
                m_statusDot->setStyleSheet("background: #F44336; border-radius: 6px;");
                m_statusLabel->setText("Ollama not running");
                m_statusLabel->setStyleSheet("font-size: 11px; color: #D32F2F;");
                m_modelCombo->clear();
                m_modelCombo->addItem("(not connected)");
            }
            reply->deleteLater();
        });
    }

private:
    QLabel *m_statusDot;
    QLabel *m_statusLabel;
    QComboBox *m_modelCombo;
    QNetworkAccessManager *m_nam;
    bool m_available = false;
};

#endif
