// test_network_policy.cpp — table-driven coverage for the cloud-free
// allowlist.  Runs under CTest as `network_policy_test`.
//
// What this protects:
//   • Loopback / RFC1918 / CGNAT / IPv6 ULA / *.local-style names all
//     pass through (cloud-free build still talks to them).
//   • Every cloud LLM host we know about is REFUSED under the same
//     policy.  Add a row here if a new provider gets a public endpoint
//     and the gating slot in aipanel/ollama starts letting it through.
#include "src/network_policy.h"

#include <QCoreApplication>
#include <QString>
#include <QUrl>

#include <cstdio>
#include <cstdlib>

namespace {
struct Case {
    const char *host;
    bool        expectedPrivate;
    const char *note;
};

constexpr Case kCases[] = {
    // ── loopback / localhost ────────────────────────────────────────
    {"localhost",                          true,  "DNS localhost"},
    {"127.0.0.1",                          true,  "IPv4 loopback"},
    {"127.255.255.254",                    true,  "IPv4 loopback upper range"},
    {"::1",                                true,  "IPv6 loopback"},
    {"[::1]",                              true,  "IPv6 loopback in brackets"},

    // ── RFC1918 ─────────────────────────────────────────────────────
    {"10.0.0.1",                           true,  "10.0.0.0/8 lower"},
    {"10.255.255.255",                     true,  "10.0.0.0/8 upper"},
    {"172.16.0.1",                         true,  "172.16/12 lower"},
    {"172.31.255.254",                     true,  "172.16/12 upper"},
    {"172.15.0.1",                         false, "172.15 is OUTSIDE 172.16/12"},
    {"172.32.0.1",                         false, "172.32 is OUTSIDE 172.16/12"},
    {"192.168.1.1",                        true,  "192.168/16 router"},
    {"192.168.255.255",                    true,  "192.168/16 upper"},

    // ── CGNAT (Tailscale / corp VPN routinely lands here) ───────────
    {"100.64.0.1",                         true,  "CGNAT lower"},
    {"100.127.255.254",                    true,  "CGNAT upper"},
    {"100.63.255.254",                     false, "OUTSIDE 100.64/10"},
    {"100.128.0.1",                        false, "OUTSIDE 100.64/10"},

    // ── link-local ──────────────────────────────────────────────────
    {"169.254.1.1",                        true,  "IPv4 link-local"},

    // ── IPv6 unique-local (fc00::/7) ────────────────────────────────
    {"fc00::1",                            true,  "IPv6 ULA fc-prefix"},
    {"fd12:3456::1",                       true,  "IPv6 ULA fd-prefix"},

    // ── private DNS suffixes ────────────────────────────────────────
    {"myhost.local",                       true,  ".local mDNS"},
    {"myhost.lan",                         true,  ".lan"},
    {"myhost.internal",                    true,  ".internal"},
    {"myhost.intranet",                    true,  ".intranet"},
    {"myhost.corp",                        true,  ".corp"},
    {"myhost.home",                        true,  ".home (mesh-VPN convention)"},
    {"ollama-server.local.",               true,  "trailing-dot FQDN"},
    {"OLLAMA.LOCAL",                       true,  "case-insensitive"},

    // ── PUBLIC LLM endpoints — MUST be refused under NOTEPATRA_NO_CLOUD ─
    {"api.openai.com",                     false, "OpenAI"},
    {"api.anthropic.com",                  false, "Anthropic / Claude"},
    {"api.mistral.ai",                     false, "Mistral"},
    {"generativelanguage.googleapis.com",  false, "Google Gemini"},
    {"openrouter.ai",                      false, "OpenRouter aggregator"},
    {"api.groq.com",                       false, "Groq"},
    {"api.together.xyz",                   false, "Together AI"},
    {"api.deepseek.com",                   false, "DeepSeek"},
    {"api.cohere.com",                     false, "Cohere"},
    {"api.x.ai",                           false, "xAI Grok"},
    {"api.perplexity.ai",                  false, "Perplexity"},
    {"api.fireworks.ai",                   false, "Fireworks AI"},

    // ── public generic ──────────────────────────────────────────────
    {"8.8.8.8",                            false, "Google DNS"},
    {"1.1.1.1",                            false, "Cloudflare DNS"},
    {"example.com",                        false, "generic public domain"},

    // ── adversarial / look-alike ────────────────────────────────────
    {"api.openai.com.evil.com",            false, "subdomain trickery"},
    {"localhost.attacker.com",             false, "label prefix trickery"},
    {"my-local",                           false, "looks-like-local but no dot"},
};

constexpr std::size_t kCaseCount = sizeof(kCases) / sizeof(kCases[0]);
}  // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    using NotepatraNetworkPolicy::isPrivateNetworkHost;

    std::size_t passed = 0;
    std::size_t failed = 0;
    for (const auto &c : kCases) {
        const bool got = isPrivateNetworkHost(QString::fromLatin1(c.host));
        if (got == c.expectedPrivate) {
            ++passed;
        } else {
            ++failed;
            std::fprintf(stderr,
                         "FAIL  %-40s  expected=%s  got=%s   (%s)\n",
                         c.host,
                         c.expectedPrivate ? "private" : "public ",
                         got                ? "private" : "public ",
                         c.note);
        }
    }

    // Also exercise the QUrl overload — scheme-aware short-circuits.
    {
        QUrl fileUrl("file:///tmp/foo");
        if (!isPrivateNetworkHost(fileUrl)) {
            std::fprintf(stderr, "FAIL  QUrl file:// was treated as public\n");
            ++failed;
        }
        QUrl httpUrl("https://api.openai.com/v1");
        if (isPrivateNetworkHost(httpUrl)) {
            std::fprintf(stderr, "FAIL  QUrl https://api.openai.com slipped through\n");
            ++failed;
        }
        QUrl httpLocal("http://127.0.0.1:11434/api/tags");
        if (!isPrivateNetworkHost(httpLocal)) {
            std::fprintf(stderr, "FAIL  QUrl http://127.0.0.1 was treated as public\n");
            ++failed;
        }
    }

    if (failed > 0) {
        std::fprintf(stderr,
                     "\nnetwork_policy: %zu PASS, %zu FAIL (of %zu cases)\n",
                     passed, failed, kCaseCount + 3);
        return EXIT_FAILURE;
    }
    std::printf("network_policy: %zu PASS, 0 FAIL\n", passed + 3);
    return EXIT_SUCCESS;
}
