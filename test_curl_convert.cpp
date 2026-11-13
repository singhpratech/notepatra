// test_curl_convert — Import cURL / Copy as cURL (pure, no widgets).
//   A  tokenizer: POSIX quoting, $'...', continuations, Windows cmd ^.
//   B  a table of real-world commands, incl. a browser "Copy as cURL".
//   C  nothing is read from disk and TLS checks are never switched off.
//   D  export is shell-safe and export -> import round-trips.

#include "curl_convert.h"

#include <QCoreApplication>
#include <QStringList>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

using namespace CurlConvert;
using Headers = QList<QPair<QString, QString>>;

static QString header(const Request &r, const QString &name) {
    for (const auto &h : r.headers)
        if (h.first.compare(name, Qt::CaseInsensitive) == 0) return h.second;
    return QString();
}

static void sectionTokenizer() {
    std::printf("-- A: tokenizer --\n");
    EXPECT("single quotes literal", tokenize("curl 'a b' '$x \"y\"'") == (QStringList{"curl", "a b", "$x \"y\""}));
    EXPECT("double quotes with escapes", tokenize("curl \"a \\\"b\\\" \\\\ c\"") == (QStringList{"curl", "a \"b\" \\ c"}));
    EXPECT("backslash line continuation", tokenize("curl \\\n  -H 'A: b' \\\r\n  https://x.test") ==
                                              (QStringList{"curl", "-H", "A: b", "https://x.test"}));
    EXPECT("$'...' ANSI-C quoting", tokenize("curl $'it\\'s\\n\\u00e9'") == (QStringList{"curl", QString::fromUtf8("it's\n\xc3\xa9")}));
    EXPECT("adjacent quoted parts join", tokenize("curl 'a'\"b\"c") == (QStringList{"curl", "abc"}));
    QString err;
    tokenize("curl 'open", &err);
    EXPECT("unterminated quote is an error", !err.isEmpty());
    EXPECT("Windows cmd ^ continuation and ^\" quotes",
           tokenize("curl ^\"https://x.test/a?b=1^&c=2^\" ^\n  -H ^\"Accept: */*^\"") ==
               (QStringList{"curl", "https://x.test/a?b=1&c=2", "-H", "Accept: */*"}));
}

static void sectionTable() {
    std::printf("-- B: real-world commands --\n");
    {
        const ImportResult r = fromCurl("curl https://api.example.com/users");
        EXPECT("plain GET", r.ok && r.request.method == "GET" && r.request.url == "https://api.example.com/users");
    }
    {
        const ImportResult r = fromCurl(
            "curl -X POST https://api.example.com/users \\\n"
            "  -H 'Content-Type: application/json' \\\n"
            "  -d '{\"name\": \"Asha\", \"role\": \"admin\"}'");
        EXPECT("POST with -X -H -d", r.ok && r.request.method == "POST" &&
                                         header(r.request, "Content-Type") == "application/json" &&
                                         r.request.body == "{\"name\": \"Asha\", \"role\": \"admin\"}");
    }
    {   // Browser DevTools "Copy as cURL (bash)" shape
        const ImportResult r = fromCurl(
            "curl 'https://api.example.com/graphql' \\\n"
            "  -H 'accept: */*' \\\n"
            "  -H 'accept-language: en-US,en;q=0.9' \\\n"
            "  -H 'content-type: application/json' \\\n"
            "  -b 'session=abc; theme=dark' \\\n"
            "  -H 'origin: https://app.example.com' \\\n"
            "  -H 'priority: u=1, i' \\\n"
            "  -H 'referer: https://app.example.com/' \\\n"
            "  -H 'sec-ch-ua: \"Chromium\";v=\"130\", \"Not?A_Brand\";v=\"99\"' \\\n"
            "  -H 'sec-fetch-mode: cors' \\\n"
            "  -H 'user-agent: Mozilla/5.0 (X11; Linux x86_64)' \\\n"
            "  --data-raw $'{\"query\":\"query { user(id: \\\\\"1\\\\\") { name } }\",\"note\":\"it\\'s\"}'");
        EXPECT("browser sample parsed", r.ok);
        EXPECT("browser: POST inferred from --data-raw", r.request.method == "POST");
        EXPECT("browser: all -H lines kept", r.request.headers.size() == 10);
        EXPECT("browser: quotes inside a header", header(r.request, "sec-ch-ua") ==
                                                      "\"Chromium\";v=\"130\", \"Not?A_Brand\";v=\"99\"");
        EXPECT("browser: -b becomes Cookie", header(r.request, "Cookie") == "session=abc; theme=dark");
        EXPECT("browser: $'...' body with escaped quotes",
               r.request.body == "{\"query\":\"query { user(id: \\\"1\\\") { name } }\",\"note\":\"it's\"}");
    }
    {   // Browser "Copy as cURL (cmd)" shape
        const ImportResult r = fromCurl(
            "curl ^\"https://api.example.com/items?a=1^&b=2^\" ^\n"
            "  -H ^\"accept: application/json^\" ^\n"
            "  --data-raw ^\"^{^\\^\"name^\\^\":^\\^\"x^\\^\"^}^\"");
        EXPECT("cmd sample parsed", r.ok && r.request.url == "https://api.example.com/items?a=1&b=2");
        EXPECT("cmd: header", header(r.request, "accept") == "application/json");
        EXPECT("cmd: body unescaped", r.request.body == "{\"name\":\"x\"}");
    }
    {
        const ImportResult r = fromCurl("curl -u alice:s3cret https://x.test/a");
        EXPECT("-u becomes Basic auth", header(r.request, "Authorization") == "Basic YWxpY2U6czNjcmV0");
    }
    {
        const ImportResult r = fromCurl("curl -A 'Agent/1' -e https://ref.test --compressed -sSL https://x.test");
        EXPECT("-A -e map to headers, --compressed ignored", r.ok && header(r.request, "User-Agent") == "Agent/1" &&
                                                                 header(r.request, "Referer") == "https://ref.test");
        EXPECT("-L inside -sSL is noted", r.notes.join(" ").contains("-L"));
    }
    {
        const ImportResult r = fromCurl("curl -G https://x.test/search -d q=rest --data-urlencode 'name=a b&c'");
        EXPECT("-G moves data to the query", r.request.method == "GET" && r.request.body.isEmpty() &&
                                                 r.request.url == "https://x.test/search?q=rest&name=a%20b%26c");
    }
    {
        const ImportResult r = fromCurl("curl --json '{\"a\":1}' --url https://x.test/j");
        EXPECT("--json sets body, method and both headers",
               r.request.method == "POST" && r.request.body == "{\"a\":1}" &&
                   header(r.request, "Content-Type") == "application/json" &&
                   header(r.request, "Accept") == "application/json" && r.request.url == "https://x.test/j");
    }
    {
        const ImportResult r = fromCurl("curl -I https://x.test");
        EXPECT("-I is HEAD", r.request.method == "HEAD");
        const ImportResult x = fromCurl("curl -XPATCH https://x.test -d a=1 -d b=2");
        EXPECT("-XPATCH attached, multiple -d joined with &", x.request.method == "PATCH" && x.request.body == "a=1&b=2");
        const ImportResult e = fromCurl("curl -H 'X-Empty;' https://x.test");
        EXPECT("'Name;' is a header with an empty value", e.request.headers.size() == 1 &&
                                                               e.request.headers[0].first == "X-Empty" &&
                                                               e.request.headers[0].second.isEmpty());
    }
    {
        EXPECT("not curl is refused", !fromCurl("wget https://x.test").ok);
        EXPECT("no URL is refused", !fromCurl("curl -H 'A: b'").ok);
        EXPECT("missing option value is refused", !fromCurl("curl https://x.test -H").ok);
    }
}

static void sectionSafety() {
    std::printf("-- C: safety --\n");
    const ImportResult f = fromCurl("curl https://x.test -d @/etc/passwd");
    EXPECT("-d @file is not read", f.ok && f.request.body.isEmpty() && f.notes.join(" ").contains("/etc/passwd"));
    const ImportResult b = fromCurl("curl https://x.test --data-binary @payload.bin --data-urlencode name@file.txt");
    EXPECT("--data-binary @ / --data-urlencode name@file not read", b.request.body.isEmpty() && b.notes.size() == 2);
    const ImportResult c = fromCurl("curl -b cookies.txt https://x.test");
    EXPECT("cookie file not read", header(c.request, "Cookie").isEmpty() && !c.notes.isEmpty());
    const ImportResult k = fromCurl("curl -k https://self-signed.test");
    EXPECT("-k noted, TLS checks stay on", k.ok && k.notes.join(" ").contains("TLS certificate checks stay on"));
}

static void sectionExport() {
    std::printf("-- D: export --\n");
    Request r;
    r.method = "POST";
    r.url = "https://x.test/a?b=1&c=$(rm -rf ~)";
    r.headers = {{"Authorization", "Bearer {{token}}"}, {"X-Quote", "it's \"here\""}, {"X-Empty", ""}};
    r.body = "{\"msg\": \"it's a `test` $HOME\"}\nline2";
    const QString cmd = toCurl(r);
    std::printf("%s\n", qPrintable(cmd));
    EXPECT("single quote escaped as '\\''", cmd.contains("'X-Quote: it'\\''s \"here\"'"));
    EXPECT("variables stay unexpanded", cmd.contains("{{token}}"));
    EXPECT("$( ) is inside single quotes", cmd.contains("'https://x.test/a?b=1&c=$(rm -rf ~)'"));
    EXPECT("multi-line with continuations", cmd.contains(" \\\n  -H "));
    const ImportResult back = fromCurl(cmd);
    EXPECT("round trip: method", back.ok && back.request.method == r.method);
    EXPECT("round trip: url", back.request.url == r.url);
    EXPECT("round trip: headers", back.request.headers == r.headers);
    EXPECT("round trip: body", back.request.body == r.body);

    const struct { const char *method; const char *body; } shapes[] = {
        {"GET", ""}, {"GET", "q"}, {"HEAD", ""}, {"DELETE", ""}, {"PUT", "x=1"}, {"OPTIONS", ""}};
    bool all = true;
    for (const auto &s : shapes) {
        Request q;
        q.method = s.method;
        q.url = "https://x.test/";
        q.body = s.body;
        const ImportResult rt = fromCurl(toCurl(q));
        all = all && rt.ok && rt.request.method == q.method && rt.request.body == q.body;
        if (!rt.ok || rt.request.method != q.method) std::printf("     mismatch for %s: %s\n", s.method, qPrintable(toCurl(q)));
    }
    EXPECT("round trip for GET/GET+body/HEAD/DELETE/PUT/OPTIONS", all);
    EXPECT("HEAD exported as --head (not -X HEAD)", toCurl(Request{"HEAD", "https://x.test", {}, {}}).contains("--head"));
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== test_curl_convert ===\n");
    sectionTokenizer();
    sectionTable();
    sectionSafety();
    sectionExport();
    std::printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
