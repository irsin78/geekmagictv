#!/bin/sh
# Install (or remove) the launchd agent that runs tools/push_usage.py every 60 seconds.
#
#   tools/install_launchd.sh 192.168.1.50     # install / update, with the display's IP
#   tools/install_launchd.sh --uninstall
#
# Log: ~/Library/Logs/smalltv-usage.log
set -eu

LABEL=com.geekmagic.usagepush
REPO=$(cd "$(dirname "$0")/.." && pwd)
TARGET="$HOME/Library/LaunchAgents/$LABEL.plist"
DOMAIN="gui/$(id -u)"

launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true

if [ "${1:-}" = "--uninstall" ]; then
  rm -f "$TARGET"
  echo "removed $LABEL"
  exit 0
fi

HOST=${1:?usage: $0 <display-ip> | --uninstall}
mkdir -p "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
sed -e "s|__SCRIPT__|$REPO/tools/push_usage.py|" -e "s|__HOME__|$HOME|g" -e "s|__HOST__|$HOST|" \
  "$REPO/tools/launchd/$LABEL.plist.template" > "$TARGET"
plutil -lint "$TARGET" >/dev/null
launchctl bootstrap "$DOMAIN" "$TARGET"
echo "installed $LABEL -> $HOST (log: ~/Library/Logs/smalltv-usage.log)"
