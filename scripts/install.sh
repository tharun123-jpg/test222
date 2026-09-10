#!/usr/bin/env bash
# =============================================================================
#  scripts/install.sh -- install the built plug-ins where After Effects looks.
#
#  Run `make plugins AE_SDK_ROOT=...` first: this script copies, it does not
#  build. On macOS it also attaches the PiPL resources, because a bundle
#  without them loads and then silently does nothing.
#
#  Usage:  sudo ./scripts/install.sh [--user]
# =============================================================================
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build/plugins"
PIPL_DIR="$REPO_ROOT/resources/pipl"

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "Nothing to install: $BUILD_DIR does not exist." >&2
    echo "Build the plug-ins first:  make plugins AE_SDK_ROOT=/path/to/sdk" >&2
    exit 1
fi

# `7.0` is the plug-in API generation, not the After Effects version. It has
# not changed in years, and one install serves every CC version on the machine.
if [[ "${1:-}" == "--user" ]]; then
    TARGET="$HOME/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
elif [[ "$(uname -s)" == "Darwin" ]]; then
    TARGET="/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
else
    TARGET="/opt/Adobe/Common/Plug-ins/7.0/MediaCore"
fi

echo "Installing into: $TARGET"
mkdir -p "$TARGET"

shopt -s nullglob

# -----------------------------------------------------------------------------
#  Attach the PiPL resources (macOS)
# -----------------------------------------------------------------------------
# Rez has to append the resource to the inner binary of each bundle, not to the
# bundle directory. Doing this before the copy means the installed bundle is
# already correct and no quarantine attribute is picked up on the way.
if [[ "$(uname -s)" == "Darwin" ]] && command -v Rez >/dev/null 2>&1; then
    for r in "$PIPL_DIR"/*.r; do
        slug="$(basename "$r" .r)"
        # motiongraphicstoolkit_anamorphicglow -> MotionGraphicsToolkit_AnamorphicGlow
        : # the bundle names below are matched by glob instead of by transcribing the case
        for bundle in "$BUILD_DIR"/*.plugin; do
            bin="$bundle/Contents/MacOS/$(basename "$bundle" .plugin)"
            [[ -f "$bin" ]] || continue
            # Match on the match name inside the resource, which is authoritative.
            if grep -qi "$(echo "$slug" | tr 'A-Z' 'a-z')" "$r" && \
               grep -qi "$(basename "$bundle" .plugin | tr 'A-Z' 'a-z' | tr -d '_')" "$r"; then
                tmp="$(mktemp)"
                Rez -o "$tmp" -append "$r"
                Rez -o "$tmp" -append "$bin"
                cp "$tmp" "$bin"
                rm -f "$tmp"
                echo "  attached $(basename "$r") -> $(basename "$bundle")"
            fi
        done
    done
else
    echo "  (skipping PiPL attachment: needs macOS and Rez -- see docs/BUILDING.md)"
fi

# -----------------------------------------------------------------------------
#  Copy
# -----------------------------------------------------------------------------
for item in "$BUILD_DIR"/*.plugin "$BUILD_DIR"/*.aex; do
    cp -R "$item" "$TARGET/"
    echo "  $(basename "$item")"
done

# Gatekeeper quarantines anything that arrived from outside the machine, and a
# quarantined plug-in fails to load with no useful message.
if [[ "$(uname -s)" == "Darwin" ]]; then
    xattr -cr "$TARGET" 2>/dev/null || true
fi

echo
echo "Done. Restart After Effects; the effects appear under"
echo "  Effects & Presets -> Motion Graphics Toolkit"
