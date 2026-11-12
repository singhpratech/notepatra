// test_http_file — the pure halves of the REST client (no widgets):
//   A  .http parser: VS Code / JetBrains shapes, CRLF, BOM, comments,
//      multi-line URL, missing blank line, "###" inside a body, 5k requests.
//   B  {{variables}}: precedence, nesting, cycles, undefined, dynamic shapes.
//   C  http-client.env.json + http-client.private.env.json.
//   D  History: redaction of literal secrets, cap, owner-only file.

#include "http_file.h"
#include "rest_history.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

using namespace HttpFile;

static const char *kVsCode =
    "@baseUrl = https://api.example.com\n"
    "### List users\n"
    "GET {{baseUrl}}/users?page=1\n"
    "Accept: application/json\n"
    "Authorization: Bearer {{token}}\n"
    "\n"
    "### Create user\n"
    "POST {{baseUrl}}/users\n"
    "Content-Type: application/json\n"
    "\n"
    "{ \"name\": \"Asha\", \"role\": \"admin\" }\n";

static void checkVsCode(const Document &d, const char *tag) {
    auto L = [&](const char *what) { return QByteArray(tag) + ": " + what; };
    EXPECT(L("two requests").constData(), d.requests.size() == 2);
    EXPECT(L("one file variable").constData(),
           d.variables.size() == 1 && d.variables[0].first == "baseUrl" &&
               d.variables[0].second == "https://api.example.com");
    if (d.requests.size() != 2) return;
    const Request &a = d.requests[0], &b = d.requests[1];
    EXPECT(L("names from ###").constData(), a.name == "List users" && b.name == "Create user");
    EXPECT(L("methods").constData(), a.method == "GET" && b.method == "POST");
    EXPECT(L("url unexpanded").constData(), a.url == "{{baseUrl}}/users?page=1" && b.url == "{{baseUrl}}/users");
    EXPECT(L("headers").constData(), a.headers.size() == 2 && a.headers[1].first == "Authorization" &&
                                         a.headers[1].second == "Bearer {{token}}");
    EXPECT(L("GET has no body").constData(), a.body.isEmpty());
    EXPECT(L("POST body exact").constData(), b.body == "{ \"name\": \"Asha\", \"role\": \"admin\" }");
    EXPECT(L("line numbers").constData(), a.startLine == 1 && a.requestLine == 2 && a.endLine == 5 &&
                                              b.startLine == 6 && b.requestLine == 7 && b.endLine == 11);
    EXPECT(L("requestAtLine").constData(), d.requestAtLine(0) == -1 && d.requestAtLine(3) == 0 &&
                                               d.requestAtLine(6) == 1 && d.requestAtLine(10) == 1);
}

static void sectionParser() {
    std::printf("-- A: parser --\n");
    checkVsCode(parse(QString::fromUtf8(kVsCode)), "LF");
    QString crlf = QString::fromUtf8(kVsCode);
    crlf.replace("\n", "\r\n");
    checkVsCode(parse(crlf), "CRLF");
    checkVsCode(parse(QChar(0xFEFF) + QString::fromUtf8(kVsCode)), "BOM");

    {   // JetBrains: version suffix, "# @name", comments, // comments in headers
        const Document d = parse(
            "# A comment\n"
            "// another\n"
            "# @name login\n"
            "POST https://httpbin.org/post HTTP/1.1\n"
            "// comment inside headers\n"
            "Content-Type: application/x-www-form-urlencoded\n"
            "\n"
            "user=a&pass=b\n"
            "\n\n"
            "###\n"
            "https://httpbin.org/ip\n");
        EXPECT("jb: two requests", d.requests.size() == 2);
        EXPECT("jb: # @name names the request", d.requests.value(0).name == "login");
        EXPECT("jb: HTTP/1.1 stripped", d.requests.value(0).url == "https://httpbin.org/post");
        EXPECT("jb: comment line inside headers skipped", d.requests.value(0).headers.size() == 1);
        EXPECT("jb: trailing blank lines dropped from body", d.requests.value(0).body == "user=a&pass=b");
        EXPECT("jb: bare URL means GET", d.requests.value(1).method == "GET" &&
                                             d.requests.value(1).url == "https://httpbin.org/ip");
    }
    {   // VS Code multi-line query
        const Document d = parse("GET https://example.com/comments\n"
                                 "    ?page=2\n"
                                 "    &pageSize=10 HTTP/1.1\n"
                                 "Accept: */*\n");
        EXPECT("multi-line URL joined", d.requests.value(0).url ==
                                            "https://example.com/comments?page=2&pageSize=10");
        EXPECT("header after continuation", d.requests.value(0).headers.size() == 1);
    }
    {   // missing blank line: a non-header line starts the body
        const Document d = parse("POST https://x.test/a\nContent-Type: application/json\n{\"a\": 1}\n");
        EXPECT("missing blank line: one header", d.requests.value(0).headers.size() == 1);
        EXPECT("missing blank line: body kept", d.requests.value(0).body == "{\"a\": 1}");
    }
    {   // "###" inside a body: only column 0 separates
        const Document d = parse("POST https://x.test/a\n\n{\n  \"note\": \"### not a separator\",\n"
                                 "  \"x\": 1\n}\n### next\nGET https://x.test/b\n");
        EXPECT("indented ### stays in the body", d.requests.size() == 2 &&
                   d.requests[0].body.contains("### not a separator") && d.requests[0].body.endsWith("}"));
        const Document d2 = parse("POST https://x.test/a\n\n\"### at column 0\"\n### at column 0\n");
        EXPECT("### at column 0 always separates (documented rule)",
               d2.requests.size() == 1 && d2.requests[0].body == "\"### at column 0\"");
    }
    {
        const Document d = parse("# only comments\n@a = 1\n\n###\n// nothing\n");
        EXPECT("blocks without a request line yield no request", d.requests.isEmpty() && d.variables.size() == 1);
        const Document f = parse("POST https://x.test\nContent-Type: application/json\n\n< ./body.json\n");
        EXPECT("< file body recognised, not read", f.requests.value(0).bodyFromFile == "./body.json");
        const Document m = parse("propfind https://dav.test/\n###\nPURGE https://cdn.test/x\n");
        EXPECT("lower-case known method upper-cased", m.requests.value(0).method == "PROPFIND");
        EXPECT("custom all-caps method", m.requests.value(1).method == "PURGE");
        const Document v = parse("@a = 1\n@b=two words\nGET https://x.test/{{a}}\n\n@notvar = in body\n");
        EXPECT("@var forms; @line in a body is body text",
               v.variables.size() == 2 && v.variables[1].second == "two words" &&
                   v.requests.value(0).body == "@notvar = in body");
    }
    {   // headers text round trip used by the panel
        const HeaderList h = headersFromText("Accept: a\n\n# c\nX-Y:  z:w \nbad line\n");
        EXPECT("headersFromText", h.size() == 2 && h[1].first == "X-Y" && h[1].second == "z:w");
        EXPECT("headersToText", headersToText(h) == "Accept: a\nX-Y: z:w");
    }
    {   // performance: 5k requests
        QString big;
        for (int i = 0; i < 5000; ++i)
            big += QStringLiteral("### r%1\nPOST https://x.test/items/%1\nContent-Type: application/json\n"
                                  "Authorization: Bearer {{token}}\n\n{\"id\": %1, \"name\": \"n%1\"}\n\n").arg(i);
        QElapsedTimer t;
        t.start();
        const Document d = parse(big);
        const qint64 ms = t.elapsed();
        std::printf("     (5000 requests parsed in %lld ms)\n", (long long)ms);
        EXPECT("5k requests parsed", d.requests.size() == 5000 && d.requests[4999].name == "r4999");
        EXPECT("5k requests under 1500 ms", ms < 1500);
        t.restart();
        int found = 0;
        for (int i = 0; i < 200; ++i) found += d.requestAtLine(i * 7 * 25) >= 0;
        EXPECT("requestAtLine on 5k is fast", t.elapsed() < 500 && found > 0);
    }
}

static void sectionVariables() {
    std::printf("-- B: variables --\n");
    QMap<QString, QString> env{{"host", "env.test"}, {"token", "env-token"}, {"loop", "{{token}}"}};
    {
        Resolver r(env, {{"host", "file.test"}});
        EXPECT("file variable beats environment", r.resolve("https://{{host}}/").text == "https://file.test/");
        EXPECT("environment used when file has none", r.resolve("{{token}}").text == "env-token");
        EXPECT("whitespace inside braces", r.resolve("{{ host }}").text == "file.test");
        EXPECT("triple brace keeps the outer brace", r.resolve("{{{host}}}").text == "{file.test}");
        EXPECT("environment value is not re-scanned (single pass)", r.resolve("{{loop}}").text == "{{token}}");
        EXPECT("non-name braces are left alone", r.resolve("{{#each x}}").ok() &&
                                                     r.resolve("{{#each x}}").text == "{{#each x}}");
    }
    {
        Resolver r(env, {{"base", "https://{{host}}/v{{ver}}"}, {"ver", "2"}, {"url", "{{base}}/users"}});
        EXPECT("nested file variables (forward and env references)",
               r.resolve("{{url}}").text == "https://env.test/v2/users");
    }
    {
        Resolver self({}, {{"a", "{{a}}"}});
        const ResolveResult rr = self.resolve("x{{a}}y");
        EXPECT("@a = {{a}} is a cycle, not a loop", !rr.ok() && rr.cycles == QStringList{"a"});
        Resolver two({}, {{"a", "{{b}}"}, {"b", "{{a}}"}});
        EXPECT("two-variable cycle detected", !two.resolve("{{a}}").ok() && !two.resolve("{{a}}").cycles.isEmpty());
    }
    {
        Resolver r(env, {});
        const ResolveResult rr = r.resolve("Bearer {{missing}} {{missing}} {{other}}");
        EXPECT("undefined names reported once each", !rr.ok() && rr.undefined == (QStringList{"missing", "other"}));
        EXPECT("undefined reference left in place (caller refuses)", rr.text.contains("{{missing}}"));
    }
    {
        Resolver r;
        const QString uuid = r.resolve("{{$uuid}}").text;
        const QRegularExpression uuidRe("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
        EXPECT("$uuid shape", uuidRe.match(uuid).hasMatch());
        EXPECT("$guid shape", uuidRe.match(r.resolve("{{$guid}}").text).hasMatch());
        EXPECT("two $uuid references differ", r.resolve("{{$uuid}}").text != uuid);
        const qint64 ts = r.resolve("{{$timestamp}}").text.toLongLong();
        EXPECT("$timestamp is unix seconds now", qAbs(ts - QDateTime::currentSecsSinceEpoch()) < 5);
        const QDateTime iso = QDateTime::fromString(r.resolve("{{$isoTimestamp}}").text, Qt::ISODateWithMs);
        EXPECT("$isoTimestamp is ISO 8601 UTC", iso.isValid() && iso.timeSpec() == Qt::UTC &&
                                                    qAbs(iso.secsTo(QDateTime::currentDateTimeUtc())) < 5);
        bool inRange = true;
        for (int i = 0; i < 50; ++i) {
            const int v = r.resolve("{{$randomInt}}").text.toInt();
            inRange = inRange && v >= 0 && v < 1000;
        }
        EXPECT("$randomInt in [0, 1000)", inRange);
        EXPECT("$randomInt min max", r.resolve("{{$randomInt 5 6}}").text == "5");
        EXPECT("$processEnv is not supported (would read the OS environment)",
               r.resolve("{{$processEnv HOME}}").undefined == QStringList{"$processEnv HOME"});
        EXPECT("$dotenv is not supported (would read a file)", !r.resolve("{{$dotenv X}}").ok());
    }
    EXPECT("referencedVariables", referencedVariables("{{a}} {{ $uuid }} {{a}} {{#x}}") == (QStringList{"a", "$uuid"}));
}

static void sectionEnvironments() {
    std::printf("-- C: environments --\n");
    const Environments e = parseEnvironments(
        R"({"$shared": {"version": "v1", "baseUrl": "shared"},
            "dev": {"baseUrl": "http://localhost:8080", "port": 8080, "debug": true, "nested": {"x": 1}},
            "staging": {"baseUrl": "https://staging.test"}})",
        R"({"staging": {"token": "private-token", "baseUrl": "https://staging-private.test"},
            "prod": {"token": "p"}})");
    EXPECT("names sorted, $shared excluded, private-only env listed",
           e.names == (QStringList{"dev", "prod", "staging"}));
    EXPECT("no parse error", e.error.isEmpty());
    const auto dev = e.variablesFor("dev");
    EXPECT("numbers and booleans become strings; objects skipped",
           dev.value("port") == "8080" && dev.value("debug") == "true" && !dev.contains("nested"));
    EXPECT("$shared applies under an environment", dev.value("version") == "v1" &&
                                                       dev.value("baseUrl") == "http://localhost:8080");
    const auto st = e.variablesFor("staging");
    EXPECT("private file merged over public", st.value("token") == "private-token" &&
                                                  st.value("baseUrl") == "https://staging-private.test");
    EXPECT("No environment = no variables", e.variablesFor(QString()).isEmpty());
    const Environments bad = parseEnvironments("{ not json", QByteArray());
    EXPECT("parse error reported with the file name", bad.error.contains("http-client.env.json"));

    QTemporaryDir dir;
    QFile pub(dir.filePath("http-client.env.json"));
    pub.open(QIODevice::WriteOnly);
    pub.write(R"({"local": {"baseUrl": "http://127.0.0.1:1"}})");
    pub.close();
    QFile priv(dir.filePath("http-client.private.env.json"));
    priv.open(QIODevice::WriteOnly);
    priv.write(R"({"local": {"token": "t"}})");
    priv.close();
    const Environments disk = loadEnvironments(dir.path());
    EXPECT("loadEnvironments reads both files", disk.names == QStringList{"local"} &&
                                                    disk.variablesFor("local").size() == 2);
    EXPECT("loadEnvironments on a folder without files", loadEnvironments(QDir::tempPath() + "/np-none-xyz").names.isEmpty());
}

static void sectionHistory() {
    std::printf("-- D: history + redaction --\n");
    using namespace RestHistory;
    EXPECT("Authorization literal redacted", redactHeaderValue("Authorization", "Bearer abc123") == "<redacted>");
    EXPECT("Authorization variable kept", redactHeaderValue("Authorization", "Bearer {{token}}") == "Bearer {{token}}");
    EXPECT("variable plus literal is redacted", redactHeaderValue("Authorization", "Bearer {{a}}xyz") == "<redacted>");
    EXPECT("Proxy-Authorization redacted", redactHeaderValue("Proxy-Authorization", "Basic Zm9vOmJhcg==") == "<redacted>");
    EXPECT("Cookie redacted", redactHeaderValue("Cookie", "sid=abc") == "<redacted>");
    EXPECT("X-Api-Key redacted", redactHeaderValue("X-Api-Key", "abc") == "<redacted>");
    EXPECT("X-Auth-Token redacted", redactHeaderValue("X-Auth-Token", "abc") == "<redacted>");
    EXPECT("Accept kept", redactHeaderValue("Accept", "application/json") == "application/json");
    EXPECT("redactHeaders keeps other lines",
           redactHeaders("Accept: a\nAuthorization: Bearer zzz\nX-Api-Key: {{k}}") ==
               "Accept: a\nAuthorization: <redacted>\nX-Api-Key: {{k}}");
    EXPECT("URL token query redacted, others kept",
           redactUrl("https://x.test/a?page=1&api_key=abc&access_token={{t}}#f") ==
               "https://x.test/a?page=1&api_key=<redacted>&access_token={{t}}#f");
    EXPECT("URL password redacted", redactUrl("https://bob:hunter2@x.test/") == "https://bob:<redacted>@x.test/");
    EXPECT("URL without secrets unchanged", redactUrl("{{baseUrl}}/users?page=1") == "{{baseUrl}}/users?page=1");
    // R20: credential headers by any "key" name, user-info containing '@'.
    EXPECT("Ocp-Apim-Subscription-Key redacted",
           redactHeaderValue("Ocp-Apim-Subscription-Key", "abc") == "<redacted>");
    EXPECT("x-functions-key redacted", redactHeaderValue("x-functions-key", "abc") == "<redacted>");
    EXPECT("any *key* header redacted", redactHeaderValue("X-Custom-Keyring", "abc") == "<redacted>");
    EXPECT("URL password containing '@' redacted whole",
           redactUrl("https://bob:p@ss@x.test/a") == "https://bob:<redacted>@x.test/a");
    EXPECT("URL token-as-user-name redacted",
           redactUrl("https://ghtoken123@x.test/a") == "https://<redacted>@x.test/a");
    EXPECT("URL user-info variable kept",
           redactUrl("https://{{user}}:{{pass}}@x.test/") == "https://{{user}}:{{pass}}@x.test/");

    // R10: bodies.
    EXPECT("sensitive field names, case and -/_ variants",
           isSensitiveField("Password") && isSensitiveField("client_secret") &&
               isSensitiveField("client-secret") && isSensitiveField("ACCESS_TOKEN") &&
               isSensitiveField("refreshToken") && isSensitiveField("id_token") &&
               isSensitiveField("api-key") && isSensitiveField("apikey") &&
               isSensitiveField("Authorization") && isSensitiveField("session_id") &&
               isSensitiveField("signature") && isSensitiveField("private_key") &&
               isSensitiveField("otp") && isSensitiveField("totp") && isSensitiveField("pin") &&
               isSensitiveField("cvv") && isSensitiveField("card_number") &&
               isSensitiveField("pwd") && isSensitiveField("pass") && isSensitiveField("passwd"));
    EXPECT("ordinary field names are not sensitive",
           !isSensitiveField("username") && !isSensitiveField("author") &&
               !isSensitiveField("footprint") && !isSensitiveField("shipping") &&
               !isSensitiveField("email") && !isSensitiveField("passenger_count"));
    {
        const QString login = "{\n  \"username\": \"asha\",\n  \"password\": \"hunter2\",\n"
                              "  \"client_secret\": \"cs-777\",\n  \"nested\": {\"Refresh-Token\": \"rt-9\","
                              " \"keep\": 1},\n  \"pin\": 1234,\n  \"card\": {\"cvv\": \"123\"},\n"
                              "  \"session\": {\"id\": \"s1\", \"exp\": 5},\n  \"token\": \"{{tok}}\"\n}";
        bool omitted = true;
        const QString red = redactBody(login, "Content-Type: application/json", &omitted);
        EXPECT("JSON body: every secret value redacted, recursively",
               !omitted && !red.contains("hunter2") && !red.contains("cs-777") && !red.contains("rt-9") &&
                   !red.contains("1234") && !red.contains("\"123\"") && !red.contains("s1"));
        EXPECT("JSON body: other fields, formatting and {{variables}} kept",
               red.contains("\"username\": \"asha\"") && red.contains("\"keep\": 1") &&
                   red.contains("\"token\": \"{{tok}}\"") && red.contains("\"password\": \"<redacted>\"") &&
                   red.startsWith("{\n  \"username\""));
        EXPECT("JSON template that is not valid JSON yet is still redacted",
               redactBody("{\"n\": {{count}}, \"password\": \"pw-1\"}", QString()) ==
                   "{\"n\": {{count}}, \"password\": \"<redacted>\"}");
        EXPECT("form body: secret fields redacted, others kept",
               redactBody("grant_type=password&username=asha&password=hunter2&client_secret=cs%2B1"
                          "&scope=read",
                          "Content-Type: application/x-www-form-urlencoded") ==
                   "grant_type=password&username=asha&password=<redacted>&client_secret=<redacted>"
                   "&scope=read");
        EXPECT("form body without a Content-Type is recognised",
               redactBody("user=a&pwd=b", QString()) == "user=a&pwd=<redacted>");
        EXPECT("small plain text body kept as typed", redactBody("hello", "Content-Type: text/plain") == "hello");
        bool om = false;
        const QString bigText = redactBody(QString(5000, QLatin1Char('x')), "Content-Type: text/plain", &om);
        EXPECT("larger plain text body omitted with size and type",
               om && bigText == "<body omitted: 5000 bytes, text/plain>");
        EXPECT("binary body omitted",
               redactBody(QStringLiteral("PK\x01\x02") + QChar(0) + "x", "Content-Type: application/zip") ==
                   "<body omitted: 6 bytes, application/zip>");
        EXPECT("Send is blocked by a redacted or omitted body",
               bodyBlocksSend(red) && bodyBlocksSend(bigText) && !bodyBlocksSend("{\"a\": 1}"));
    }

    QTemporaryDir dir;
    const QString path = dir.filePath("rest-client.json");
    Entry e;
    e.time = QDateTime::currentDateTime();
    e.method = "GET";
    e.url = "https://x.test/a?token=SECRETVALUE1";
    e.headers = "Authorization: Bearer SECRETVALUE2\nAccept: */*";
    e.status = 200;
    EXPECT("append writes", append(path, e));
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    const QByteArray raw = f.readAll();
    f.close();
    EXPECT("no secret literal reaches the disk", !raw.contains("SECRETVALUE1") && !raw.contains("SECRETVALUE2"));
    EXPECT("redaction marker written", raw.contains("<redacted>"));
#ifndef Q_OS_WIN
    EXPECT("history file is owner-only (0600)",
           QFile::permissions(path) == (QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                        QFileDevice::ReadUser | QFileDevice::WriteUser));
#endif
    for (int i = 0; i < 60; ++i) {
        e.url = QStringLiteral("https://x.test/%1").arg(i);
        append(path, e);
    }
    const QList<Entry> all = load(path);
    EXPECT("history capped at 50, newest first", all.size() == kMaxEntries && all.first().url == "https://x.test/59");
    Entry big = e;
    big.body = QString(kMaxStoredBody + 1, QLatin1Char('x'));
    append(path, big);
    EXPECT("oversized body omitted (stored as the omitted marker)",
           load(path).first().bodyOmitted && load(path).first().body.startsWith("<body omitted: "));
    Entry login = e;
    login.headers = "Content-Type: application/json";
    login.body = "{\"user\": \"a\", \"password\": \"BODYSECRET3\", \"client_secret\": \"BODYSECRET4\"}";
    append(path, login);
    QFile f2(path);
    f2.open(QIODevice::ReadOnly);
    const QByteArray raw2 = f2.readAll();
    // Closed before the next write: on Windows an open handle blocks the
    // save's rename over the file.
    f2.close();
    EXPECT("R10: a login body's password / client_secret never reach rest-client.json",
           !raw2.contains("BODYSECRET3") && !raw2.contains("BODYSECRET4") &&
               load(path).first().body.contains("\"user\": \"a\""));
    EXPECT("environment remembered per folder", setEnvironmentFor(path, "/some/dir", "staging") &&
                                                     environmentFor(path, "/some/dir") == "staging");
    EXPECT("clear empties history", clear(path) && load(path).isEmpty());
    EXPECT("clear keeps the environment choice", environmentFor(path, "/some/dir") == "staging");
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== test_http_file ===\n");
    sectionParser();
    sectionVariables();
    sectionEnvironments();
    sectionHistory();
    std::printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
