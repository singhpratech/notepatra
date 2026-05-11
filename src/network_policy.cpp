#include "network_policy.h"

#include <QAbstractSocket>
#include <QHostAddress>

namespace NotepatraNetworkPolicy {

static bool hostMatchesPrivateName(const QString &host) {
    if (host.isEmpty()) return false;

    QString h = host;
    if (h.endsWith(QLatin1Char('.'))) h.chop(1);   // strip FQDN trailing dot
    h = h.toLower();

    if (h == QLatin1String("localhost")) return true;

    // Private DNS suffixes used in corporate / lab / mesh-VPN networks.
    static const char *const suffixes[] = {
        ".local", ".lan", ".internal", ".intranet",
        ".corp",  ".home",
    };
    for (const char *s : suffixes) {
        if (h.endsWith(QLatin1String(s))) return true;
    }
    return false;
}

static bool ipIsPrivate(const QHostAddress &addr) {
    if (addr.isLoopback()) return true;
    if (addr.isLinkLocal()) return true;

    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 v = addr.toIPv4Address();
        // 10.0.0.0/8
        if ((v & 0xFF000000u) == 0x0A000000u) return true;
        // 172.16.0.0/12
        if ((v & 0xFFF00000u) == 0xAC100000u) return true;
        // 192.168.0.0/16
        if ((v & 0xFFFF0000u) == 0xC0A80000u) return true;
        // 100.64.0.0/10 — CGNAT (Tailscale, corp NAT).
        if ((v & 0xFFC00000u) == 0x64400000u) return true;
        return false;
    }
    if (addr.protocol() == QAbstractSocket::IPv6Protocol) {
        Q_IPV6ADDR raw = addr.toIPv6Address();
        // fc00::/7 — IPv6 Unique-Local (RFC 4193).
        if ((raw[0] & 0xFE) == 0xFC) return true;
        return false;
    }
    return false;
}

bool isPrivateNetworkHost(const QString &host) {
    if (host.isEmpty()) return false;

    // Strip IPv6 bracket form: "[::1]" → "::1".
    QString h = host;
    if (h.startsWith(QLatin1Char('[')) && h.endsWith(QLatin1Char(']'))) {
        h = h.mid(1, h.size() - 2);
    }

    QHostAddress addr(h);
    if (!addr.isNull()) return ipIsPrivate(addr);

    return hostMatchesPrivateName(h);
}

bool isPrivateNetworkHost(const QUrl &url) {
    const QString scheme = url.scheme();
    // file://, unix://, qrc://, empty — never network destinations.
    if (scheme == QLatin1String("file")  ||
        scheme == QLatin1String("unix")  ||
        scheme == QLatin1String("qrc")   ||
        scheme.isEmpty()) {
        return true;
    }
    return isPrivateNetworkHost(url.host());
}

}  // namespace NotepatraNetworkPolicy
