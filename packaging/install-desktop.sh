#!/usr/bin/env bash
#
# Install exPlayer into the user's desktop environment:
#   - symlinks ~/.local/bin/explayer to the newest local build artifact
#   - installs the app icon, .desktop launcher and module MIME types
#   - registers exPlayer as the default handler for its audio types
#     (and for video types too when run with --video)
#
# No sudo required. Re-run after switching build directories or moving the repo.

set -euo pipefail

# Resolve paths relative to this script so it works from any CWD.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

BINARY=""
CANDIDATES=(
    "$PROJECT_DIR/build/exPlayer_artefacts/exPlayer"
    "$PROJECT_DIR/build/exPlayer_artefacts/Release/exPlayer"
    "$PROJECT_DIR/build/exPlayer_artefacts/Debug/exPlayer"
    "$PROJECT_DIR/cmake-build-debug/exPlayer_artefacts/Debug/exPlayer"
    "$PROJECT_DIR/cmake-build-release/exPlayer_artefacts/Release/exPlayer"
)

DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
BIN_DIR="$HOME/.local/bin"
APP_DIR="$DATA_HOME/applications"
ICON_DIR="$DATA_HOME/icons/hicolor/scalable/apps"
MIME_DIR="$DATA_HOME/mime/packages"

DESKTOP_FILE="explayer.desktop"
MIME_TYPES=(
    audio/mpeg audio/x-wav audio/wav audio/flac
    audio/ogg audio/x-vorbis+ogg
    audio/x-explayer-module
    audio/x-mod audio/x-xm audio/x-it audio/x-s3m
    audio/x-extracker-xtp
    audio/mp4 audio/x-m4a audio/aiff audio/x-aiff
)
# Video types are listed in the .desktop file, so exPlayer shows up under
# "Open With", but are only made the default with --video.
VIDEO_MIME_TYPES=(
    video/mp4 video/x-matroska video/webm video/x-msvideo video/quicktime
    video/x-m4v video/x-flv video/x-ms-wmv video/mpeg video/mp2t video/ogg
)
if [[ "${1:-}" == "--video" ]]; then
    MIME_TYPES+=("${VIDEO_MIME_TYPES[@]}")
fi

latest_mtime=0
for candidate in "${CANDIDATES[@]}"; do
    if [[ -x "$candidate" ]]; then
        candidate_mtime=$(stat -c '%Y' "$candidate")
        if (( candidate_mtime > latest_mtime )); then
            latest_mtime=$candidate_mtime
            BINARY="$candidate"
        fi
    fi
done

if [[ -z "$BINARY" ]]; then
    echo "error: built binary not found at:" >&2
    for candidate in "${CANDIDATES[@]}"; do
        echo "  $candidate" >&2
    done
    echo "Build it first, e.g.:" >&2
    echo "  (cd \"$PROJECT_DIR\" && cmake -S . -B build && cmake --build build --config Release)" >&2
    exit 1
fi

mkdir -p "$BIN_DIR" "$APP_DIR" "$ICON_DIR" "$MIME_DIR"

# exPlayer used to be called SoundPlayer2: remove the old launcher, menu entry,
# icon and MIME file so the menu doesn't show both. (Settings are kept: exPlayer
# copies ~/.config/SoundPlayer2 to ~/.config/exPlayer on its first start.)
for old in "$BIN_DIR/soundplayer2" "$APP_DIR/soundplayer2.desktop" \
           "$ICON_DIR/soundplayer2.svg" "$MIME_DIR/soundplayer2.xml"; do
    if [[ -e "$old" || -L "$old" ]]; then
        echo "Removing old SoundPlayer2 file -> $old"
        rm -f "$old"
    fi
done

echo "Linking launcher -> $BIN_DIR/explayer"
rm -f "$BIN_DIR/explayer"
ln -s "$BINARY" "$BIN_DIR/explayer"

echo "Installing icon -> $ICON_DIR/explayer.svg"
install -m 0644 "$SCRIPT_DIR/explayer.svg" "$ICON_DIR/explayer.svg"

echo "Installing MIME types -> $MIME_DIR/explayer.xml"
install -m 0644 "$SCRIPT_DIR/explayer-mime.xml" "$MIME_DIR/explayer.xml"
if command -v update-mime-database >/dev/null 2>&1; then
    update-mime-database "$DATA_HOME/mime" >/dev/null 2>&1 || true
fi

echo "Installing launcher -> $APP_DIR/$DESKTOP_FILE"
install -m 0644 "$SCRIPT_DIR/$DESKTOP_FILE" "$APP_DIR/$DESKTOP_FILE"
# Use the launcher's absolute path: the desktop session's PATH often lacks
# ~/.local/bin until the next login, so a bare "explayer" would not launch.
sed -i "s|^Exec=explayer |Exec=$BIN_DIR/explayer |" "$APP_DIR/$DESKTOP_FILE"
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$APP_DIR" >/dev/null 2>&1 || true
fi

if command -v xdg-mime >/dev/null 2>&1; then
    echo "Registering default file associations"
    for mt in "${MIME_TYPES[@]}"; do
        xdg-mime default "$DESKTOP_FILE" "$mt" 2>/dev/null || true
    done
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$DATA_HOME/icons/hicolor" >/dev/null 2>&1 || true
fi

echo
echo "Done. exPlayer should now appear in your application menu."
echo "Desktop launcher target: $BINARY"

case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *)
        echo
        echo "note: $BIN_DIR is not on your PATH."
        echo "      The menu entry still works; to run 'explayer' from a shell, add:"
        echo "        export PATH=\"\$HOME/.local/bin:\$PATH\""
        ;;
esac
