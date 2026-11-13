// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_csv_column_name_guidance — the csv_query tool description and the
// Data Analyst prompt must tell the model to use the column names shown in
// the schema/preview. Regression: both said "column names match the CSV
// header", but headers are sanitized (`Unit Price` -> `Unit_Price`), so a
// model that trusted the text queried columns that do not exist.

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

#include "ai_systemprompt.h"
#include "ai_tools.h"

static int g_pass = 0, g_fail = 0;

static void check(const char *label, bool ok) {
    if (ok) { ++g_pass; std::printf("  [PASS] %s\n", label); }
    else    { ++g_fail; std::printf("  [FAIL] %s\n", label); }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);

    QString csvTool;
    for (const QJsonValue &v : AiTools::availableTools()) {
        const QString s = QString::fromUtf8(QJsonDocument(v.toObject()).toJson());
        if (s.contains("\"csv_query\"")) csvTool = s;
    }
    check("csv_query tool is defined (vacuity)", !csvTool.isEmpty());
    check("csv_query: no 'match the CSV header' claim", !csvTool.contains("match the CSV header"));
    check("csv_query: points at the schema/preview names",
          csvTool.contains("exact column names shown in the schema/preview"));

    const QString prompt = AiSystemPrompt::build(AiSystemPrompt::Intent::DataAnalyst, QString(),
                                                 /*toolsActive=*/true);
    check("data prompt mentions csv_query (vacuity)", prompt.contains("csv_query"));
    check("data prompt: no 'match the CSV header' claim", !prompt.contains("match the CSV"));
    check("data prompt: points at the schema/preview names",
          prompt.contains("exact column names shown in the schema/preview"));

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
