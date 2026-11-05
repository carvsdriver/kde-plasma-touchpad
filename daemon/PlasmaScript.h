#pragma once

#include <QString>

namespace PlasmaTP {

// Run a script through plasmashell's org.kde.PlasmaShell.evaluateScript and
// return what it print()ed, or an empty string on failure.
QString runPlasmaScript(const QString &script);

} // namespace PlasmaTP
