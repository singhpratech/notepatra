#include "credscrub.h"

#include <QRegularExpression>
#include <QVector>

namespace CredScrub {

namespace {

struct Rule {
    QRegularExpression re;
    QString marker;
};

// One-time-built table of vendor-specific credential patterns. Entries are
// ordered "most specific first" — Anthropic's `sk-ant-` prefix matches before
// the generic `sk-` rule, so the redaction marker is informative
// ("[REDACTED-ANTHROPIC-KEY]") instead of the catch-all.
//
// Each pattern is anchored on a recognizable prefix and a length floor that
// matches the issuer's real key shape, to keep false positives low.
static const QVector<Rule> &rules() {
    static const QVector<Rule> kRules = []{
        QVector<Rule> r;
        auto add = [&](const QString &pat, const QString &marker) {
            QRegularExpression re(pat);
            re.optimize();
            r.push_back({re, marker});
        };
        // OpenRouter — sk-or-v1-<64+ hex/alnum>
        add(QStringLiteral("sk-or-v\\d+-[A-Za-z0-9]{40,}"),
            QStringLiteral("[REDACTED-OPENROUTER-KEY]"));
        // Anthropic — sk-ant-api03-<>
        add(QStringLiteral("sk-ant-(?:api\\d{2}-)?[A-Za-z0-9_-]{40,}"),
            QStringLiteral("[REDACTED-ANTHROPIC-KEY]"));
        // OpenAI project keys — sk-proj-<long>
        add(QStringLiteral("sk-proj-[A-Za-z0-9_-]{40,}"),
            QStringLiteral("[REDACTED-OPENAI-KEY]"));
        // OpenAI legacy — sk-<long alnum>
        add(QStringLiteral("sk-[A-Za-z0-9]{40,}"),
            QStringLiteral("[REDACTED-OPENAI-KEY]"));
        // GitHub fine-grained PAT
        add(QStringLiteral("github_pat_[A-Za-z0-9_]{40,}"),
            QStringLiteral("[REDACTED-GITHUB-TOKEN]"));
        // GitHub classic PAT / OAuth / server / user tokens
        add(QStringLiteral("gh[posu]_[A-Za-z0-9]{36}"),
            QStringLiteral("[REDACTED-GITHUB-TOKEN]"));
        // GitLab PAT
        add(QStringLiteral("glpat-[A-Za-z0-9_-]{20,}"),
            QStringLiteral("[REDACTED-GITLAB-TOKEN]"));
        // AWS access key id (literal AKIA + 16 upper alnum)
        add(QStringLiteral("\\bAKIA[A-Z0-9]{16}\\b"),
            QStringLiteral("[REDACTED-AWS-ACCESS-KEY-ID]"));
        // AWS temporary keys (ASIA…)
        add(QStringLiteral("\\bASIA[A-Z0-9]{16}\\b"),
            QStringLiteral("[REDACTED-AWS-ACCESS-KEY-ID]"));
        // Slack tokens
        add(QStringLiteral("xox[baprs]-[A-Za-z0-9-]{10,}"),
            QStringLiteral("[REDACTED-SLACK-TOKEN]"));
        // Stripe live/test keys
        add(QStringLiteral("(?:sk|pk|rk)_(?:live|test)_[A-Za-z0-9]{20,}"),
            QStringLiteral("[REDACTED-STRIPE-KEY]"));
        // SendGrid API key
        add(QStringLiteral("SG\\.[A-Za-z0-9_-]{20,}\\.[A-Za-z0-9_-]{30,}"),
            QStringLiteral("[REDACTED-SENDGRID-KEY]"));
        // Google API key — AIza + 35 alnum-ish
        add(QStringLiteral("\\bAIza[A-Za-z0-9_-]{35}\\b"),
            QStringLiteral("[REDACTED-GOOGLE-API-KEY]"));
        // JWT — three base64url segments separated by dots
        add(QStringLiteral("\\beyJ[A-Za-z0-9_-]{8,}\\.eyJ[A-Za-z0-9_-]{8,}\\.[A-Za-z0-9_-]{8,}\\b"),
            QStringLiteral("[REDACTED-JWT]"));
        // PEM private key blocks — block out the entire base64 body so the
        // marker doesn't leak the algorithm header
        add(QStringLiteral("-----BEGIN (?:RSA |OPENSSH |EC |DSA |PGP |ENCRYPTED |)PRIVATE KEY-----[\\s\\S]*?-----END (?:RSA |OPENSSH |EC |DSA |PGP |ENCRYPTED |)PRIVATE KEY-----"),
            QStringLiteral("[REDACTED-PRIVATE-KEY-BLOCK]"));
        // Generic key=value at the end (use last so it doesn't preempt the
        // vendor-specific rules above). Matches assignments like
        // password = "hunter2", api_key: "abc...", token=xxxxxx where the
        // value has at least 12 high-entropy-ish chars.
        add(QStringLiteral("(?i)\\b(?:password|passwd|pwd|secret|api[_-]?key|apikey|access[_-]?token|auth[_-]?token|bearer)\\s*[:=]\\s*[\"']?([A-Za-z0-9+/_=-]{16,})[\"']?"),
            QStringLiteral("[REDACTED-SECRET]"));
        return r;
    }();
    return kRules;
}

}  // namespace

QString redact(const QString &text, int *redactedCount) {
    if (text.isEmpty()) {
        if (redactedCount) *redactedCount = 0;
        return text;
    }

    QString out = text;
    int total = 0;
    for (const Rule &rule : rules()) {
        // Walk every match by hand so we can count occurrences and substitute
        // the appropriate per-rule marker. QString::replace(QRegularExpression,
        // QString) doesn't expose the count.
        QRegularExpressionMatchIterator it = rule.re.globalMatch(out);
        QString rebuilt;
        rebuilt.reserve(out.size());
        int cursor = 0;
        int hitsThisRule = 0;
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            const int start = m.capturedStart();
            const int end   = m.capturedEnd();
            if (start < 0) continue;
            rebuilt.append(out.midRef(cursor, start - cursor));
            // For the generic key=value rule, preserve the assignment prefix
            // so the model still sees that there *was* a secret here, just
            // not its value. Capture group 1 is the value; replace just that.
            if (m.capturedTexts().size() >= 2 && m.capturedStart(1) >= 0) {
                const int valueStart = m.capturedStart(1);
                const int valueEnd   = m.capturedEnd(1);
                rebuilt.append(out.midRef(start, valueStart - start));
                rebuilt.append(rule.marker);
                rebuilt.append(out.midRef(valueEnd, end - valueEnd));
            } else {
                rebuilt.append(rule.marker);
            }
            cursor = end;
            ++hitsThisRule;
        }
        if (hitsThisRule > 0) {
            rebuilt.append(out.midRef(cursor));
            out = rebuilt;
            total += hitsThisRule;
        }
    }

    if (redactedCount) *redactedCount = total;
    return out;
}

}  // namespace CredScrub
