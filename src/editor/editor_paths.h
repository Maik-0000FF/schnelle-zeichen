// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCHNELLE_ZEICHEN_EDITOR_PATHS_H
#define SCHNELLE_ZEICHEN_EDITOR_PATHS_H

// One place for the editor's absolute config dir (QString side), so the
// models don't each re-assemble it. The resolution itself lives in
// core/config_dir.h (shared with the engine); this only adds the QString
// wrapper and the trailing slash the Qt-side path concatenation expects.

#include <QString>

#include "core/config_dir.h"

namespace schnelle_zeichen {

inline QString configDirPath() {
    return QString::fromStdString(configDir()) + QStringLiteral("/");
}

} // namespace schnelle_zeichen

#endif // SCHNELLE_ZEICHEN_EDITOR_PATHS_H
