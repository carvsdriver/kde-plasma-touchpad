#pragma once

#include <QString>

namespace PlasmaTP {

// Load `script` into KWin under the plugin name `name` and run it.  A script
// already loaded under that name is unloaded first, so the new content is
// what runs.  A script that connects to workspace signals keeps running until
// unloadKWinScript(name).  Returns false if KWin could not load it.
bool runKWinScript(const QString &name, const QString &script);
void unloadKWinScript(const QString &name);

} // namespace PlasmaTP
