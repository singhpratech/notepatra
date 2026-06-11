// SPDX-License-Identifier: GPL-3.0-or-later
#include "cliargs.h"
#include <QFileInfo>

CliArgs parseCliArgs(const QStringList &args) {
    CliArgs out;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QLatin1String("--line") && i + 1 < args.size()) {
            // Only consume the next arg when it actually parses as a line
            // number — `--line file.py` (forgotten N) must not eat the file.
            bool ok = false;
            const int n = args.at(i + 1).toInt(&ok);
            if (ok) {
                out.gotoLine = n;
                ++i;
            }
        } else if (arg == QLatin1String("--theme") && i + 1 < args.size()) {
            out.theme = args.at(++i);
        } else if (arg == QLatin1String("-n") || arg == QLatin1String("--new")) {
            out.newWindow = true;
        } else if (!arg.startsWith(QLatin1Char('-'))) {
            // Unknown "-" flags stay silently ignored (also covers macOS -psn_*).
            const QFileInfo fi(arg);
            if (fi.isFile())
                out.files.append(fi.absoluteFilePath());
            else
                // Absolutize even when missing: not-found args ride the
                // forward payload to the primary, which must not re-resolve
                // a relative name against ITS cwd — a file of the same name
                // there would silently open instead.
                out.notFound.append(fi.absoluteFilePath());
        }
    }
    return out;
}
