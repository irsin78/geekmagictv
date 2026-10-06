#!/bin/sh
# Install (or remove) the launchd agent that runs tools/push_usage.py every 60 seconds.
#
#   tools/install_launchd.sh 192.168.1.50 <token>   # install / update: display IP + push token
#                                                   # (token shown on the display's web page; omit it
#                                                   # for your own build, which reads secrets.h)
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

HOST=${1:?usage: $0 <display-ip> [push-token] | --uninstall}
TOKEN=${2:-}
mkdir -p "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
sed -e "s|__SCRIPT__|$REPO/tools/push_usage.py|" -e "s|__HOME__|$HOME|g" -e "s|__HOST__|$HOST|" -e "s|__TOKEN__|$TOKEN|" \
  "$REPO/tools/launchd/$LABEL.plist.template" > "$TARGET"
chmod 600 "$TARGET"  # it holds the push token
plutil -lint "$TARGET" >/dev/null
launchctl bootstrap "$DOMAIN" "$TARGET"
echo "installed $LABEL -> $HOST (log: ~/Library/Logs/smalltv-usage.log)"
