// Test fixtures here are constructed at runtime by concatenating a vendor
// prefix with a synthetic body. The source file deliberately avoids contiguous
// strings that match any real-secret regex so static secret-scanners (GitHub,
// Gitleaks, etc.) don't flag this test as containing live credentials.
#include "src/credscrub.h"

#include <QString>
#include <QtGlobal>
#include <cstdio>
#include <cstdlib>

#define EXPECT(cond) \
    do { if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s line %d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } } while (0)

#define EXPECT_EQ(actual, expected) \
    do { auto _a = (actual); auto _e = (expected); \
        if (_a != _e) { \
            std::fprintf(stderr, "FAIL: %s line %d:\n  actual:   %s\n  expected: %s\n", \
                __FILE__, __LINE__, QString(_a).toUtf8().constData(), \
                QString(_e).toUtf8().constData()); \
            std::exit(1); \
        } } while (0)

#define CHECK_REDACTED(input, marker) \
    do { int n = 0; QString out = CredScrub::redact(QString(input), &n); \
        if (n == 0) { \
            std::fprintf(stderr, "FAIL: %s line %d: expected redaction in:\n  %s\n  got: %s\n", \
                __FILE__, __LINE__, QString(input).toUtf8().constData(), out.toUtf8().constData()); \
            std::exit(1); \
        } \
        if (!out.contains(marker)) { \
            std::fprintf(stderr, "FAIL: %s line %d: expected marker '%s' in:\n  %s\n", \
                __FILE__, __LINE__, QString(marker).toUtf8().constData(), out.toUtf8().constData()); \
            std::exit(1); \
        } \
        if (out.contains(input)) { \
            std::fprintf(stderr, "FAIL: %s line %d: original secret survived redaction:\n  %s\n", \
                __FILE__, __LINE__, out.toUtf8().constData()); \
            std::exit(1); \
        } \
    } while (0)

// Build prefix tokens at runtime so the file never has a contiguous secret-
// shaped literal. Each helper returns a complete vendor-shaped synthetic blob.
static QString joinKey(const char *prefix, const QString &body) {
    return QString::fromLatin1(prefix) + body;
}

int main() {
    // Synthetic bodies — generic alnum filler, never copy-pasted from a real key.
    const QString hex64 = QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    const QString alnum48 = QStringLiteral("abcDEF0123456789-_abcDEF0123456789-_abcDEF0123xx");
    const QString alnum56 = QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123ABCD");
    const QString alnum44 = QStringLiteral("0123456789abcdefABCDEF0123456789abcdefABCDEF");
    const QString gh36   = QStringLiteral("0123456789ABCDEFabcdef0123456789ABCD");
    const QString ghpat53 = QStringLiteral("0123456789ABCDEFabcdef0123456789ABCDEFabcdef0123456789");
    const QString aizaB = QStringLiteral("SyA0123456789abcdef0123456789ABCDEF");
    const QString slackB = QStringLiteral("1234567890-1234567890-abcDEF0123abcDEF0123");
    const QString stripeB = QStringLiteral("0123456789abcdefABCDEF01");
    const QString awsB    = QStringLiteral("IOSFODNN7EXAMPLE");

    // ── Empty / no-op ───────────────────────────────────────────────────
    {
        int n = -1;
        QString out = CredScrub::redact("", &n);
        EXPECT(out.isEmpty());
        EXPECT_EQ(n, 0);
    }
    {
        int n = -1;
        QString out = CredScrub::redact("a normal sentence with no secrets in it", &n);
        EXPECT_EQ(n, 0);
        EXPECT_EQ(out, QString("a normal sentence with no secrets in it"));
    }
    // Code that *looks* tokenish but isn't a credential — must not redact.
    {
        int n = -1;
        QString out = CredScrub::redact(
            "commit a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2 for refactor\n"
            "uuid 550e8400-e29b-41d4-a716-446655440000\n"
            "color #abcdef and base64 'aGVsbG8gd29ybGQK'\n", &n);
        EXPECT_EQ(n, 0);
    }

    // ── OpenRouter ──────────────────────────────────────────────────────
    {
        QString line = "headers['Authorization'] = 'Bearer "
                       + joinKey("sk-" "or-v1-", hex64) + "'";
        CHECK_REDACTED(line, "[REDACTED-OPENROUTER-KEY]");
    }

    // ── Anthropic ───────────────────────────────────────────────────────
    {
        QString line = "ANTHROPIC_API_KEY=" + joinKey("sk-" "ant-api03-", alnum48);
        CHECK_REDACTED(line, "[REDACTED-ANTHROPIC-KEY]");
    }

    // ── OpenAI proj / legacy ────────────────────────────────────────────
    {
        QString line = "OPENAI=" + joinKey("sk-" "proj-", alnum56);
        CHECK_REDACTED(line, "[REDACTED-OPENAI-KEY]");
    }
    {
        QString line = "old-style: " + joinKey("sk-", alnum44);
        CHECK_REDACTED(line, "[REDACTED-OPENAI-KEY]");
    }

    // ── GitHub tokens ───────────────────────────────────────────────────
    {
        QString line = "GITHUB_TOKEN=" + joinKey("ghp_", gh36);
        CHECK_REDACTED(line, "[REDACTED-GITHUB-TOKEN]");
    }
    {
        QString line = "GITHUB_TOKEN=" + joinKey("github_" "pat_", ghpat53);
        CHECK_REDACTED(line, "[REDACTED-GITHUB-TOKEN]");
    }

    // ── AWS ─────────────────────────────────────────────────────────────
    {
        QString line = "aws_access_key_id = " + joinKey("AKIA", awsB);
        CHECK_REDACTED(line, "[REDACTED-AWS-ACCESS-KEY-ID]");
    }

    // ── Google API key ──────────────────────────────────────────────────
    {
        QString line = "GOOGLE_API_KEY=" + joinKey("AIza", aizaB);
        CHECK_REDACTED(line, "[REDACTED-GOOGLE-API-KEY]");
    }

    // ── Slack ───────────────────────────────────────────────────────────
    {
        QString line = "SLACK_BOT=" + joinKey("xoxb-", slackB);
        CHECK_REDACTED(line, "[REDACTED-SLACK-TOKEN]");
    }

    // ── Stripe ──────────────────────────────────────────────────────────
    {
        QString line = "STRIPE=" + joinKey("sk_" "live_", stripeB);
        CHECK_REDACTED(line, "[REDACTED-STRIPE-KEY]");
    }

    // ── JWT ─────────────────────────────────────────────────────────────
    {
        // Three-part base64url joined with '.' — built up at runtime so the
        // source has no contiguous JWT-shaped literal.
        QString header  = QStringLiteral("eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9");
        QString payload = QStringLiteral("eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ");
        QString sig     = QStringLiteral("SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c");
        QString line = "JWT=" + header + "." + payload + "." + sig;
        CHECK_REDACTED(line, "[REDACTED-JWT]");
    }

    // ── PEM private key block ───────────────────────────────────────────
    {
        QString begin = "-----BEGIN " + QString("RSA ") + "PRIVATE KEY-----";
        QString end   = "-----END "   + QString("RSA ") + "PRIVATE KEY-----";
        QString line = begin + "\n"
            "MIIEowIBAAKCAQEAtESTKEYDATAaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
            "test-key-data-only-for-unit-tests\n"
            + end;
        CHECK_REDACTED(line, "[REDACTED-PRIVATE-KEY-BLOCK]");
    }

    // ── Generic key=value ───────────────────────────────────────────────
    {
        QString line = "password = \"hunter2hunter2hunter2\"";
        int n = 0;
        QString out = CredScrub::redact(line, &n);
        EXPECT(n >= 1);
        EXPECT(out.contains("[REDACTED-SECRET]"));
        EXPECT(!out.contains("hunter2hunter2hunter2"));
        // Must preserve the "password = " prefix so the model still sees the
        // shape of the assignment, just not the value.
        EXPECT(out.contains("password"));
    }

    // ── Mixed: multi-line scrubbing with leading non-secret content ─────
    {
        QString combined =
            "hi\n"
            "\n"
            + joinKey("sk-" "or-v1-", hex64);
        int n = 0;
        QString out = CredScrub::redact(combined, &n);
        EXPECT_EQ(n, 1);
        EXPECT(out.contains("[REDACTED-OPENROUTER-KEY]"));
        EXPECT(!out.contains(hex64));
        EXPECT(out.startsWith("hi\n"));
    }

    // ── Multiple secrets in one document ────────────────────────────────
    {
        QString doc =
            QString("config = {\n"
                    "  openai: '") + joinKey("sk-", alnum44) + "',\n"
            "  github: '" + joinKey("ghp_", gh36) + "',\n"
            "  aws_id: '" + joinKey("AKIA", awsB) + "',\n"
            "}\n";
        int n = 0;
        QString out = CredScrub::redact(doc, &n);
        EXPECT(n >= 3);
        EXPECT(!out.contains(alnum44));
        EXPECT(!out.contains(gh36));
        EXPECT(!out.contains(awsB));
        EXPECT(out.contains("[REDACTED-OPENAI-KEY]"));
        EXPECT(out.contains("[REDACTED-GITHUB-TOKEN]"));
        EXPECT(out.contains("[REDACTED-AWS-ACCESS-KEY-ID]"));
    }

    std::printf("test_credscrub: all assertions passed\n");
    return 0;
}
