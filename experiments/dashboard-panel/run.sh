#!/bin/bash
# Dashboard panel experiment driver.
#
#   ./run.sh create [height]   create the panel (default height 452)
#   ./run.sh measure           print panel and widget geometry
#   ./run.sh remove            remove the panel
#
# YOUTUBE_URL and NEWS_URL can be overridden from the environment.

set -euo pipefail
cd "$(dirname "$0")"

YOUTUBE_URL=${YOUTUBE_URL:-https://m.youtube.com/}
NEWS_URL=${NEWS_URL:-https://text.npr.org/}

eval_script() {
    busctl --user call org.kde.plasmashell /PlasmaShell \
        org.kde.PlasmaShell evaluateScript s "$1" |
        sed -e 's/^s "//' -e 's/"$//' -e 's/\\n/\n/g'
}

case "${1:-}" in
create)
    height=${2:-452}
    script=$(sed -e "s|@HEIGHT@|$height|" \
                 -e "s|@YOUTUBE_URL@|$YOUTUBE_URL|" \
                 -e "s|@NEWS_URL@|$NEWS_URL|" create.js)
    eval_script "$script"
    ;;
measure)
    eval_script "$(cat measure.js)"
    ;;
remove)
    eval_script "$(cat remove.js)"
    ;;
*)
    echo "usage: $0 create [height] | measure | remove" >&2
    exit 1
    ;;
esac
