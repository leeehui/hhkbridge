#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# ESP-IDF v5.x Install Script for Arch Linux (no sudo needed)
# Target: ESP32-S3
# All tools installed to ~/esp/ and ~/.espressif/
# ============================================================

IDF_VER="${IDF_VER:-v5.5.4}"
IDF_TARGET="${IDF_TARGET:-esp32s3}"
IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"

echo "=========================================="
echo " ESP-IDF Install for Arch Linux"
echo " Version:  $IDF_VER"
echo " Target:   $IDF_TARGET"
echo " IDF Path: $IDF_PATH"
echo " Tools:    $HOME/.espressif/"
echo "=========================================="
echo ""

# ---- 1. Clone ESP-IDF (shallow, ~500MB) ----
if [ -d "$IDF_PATH" ]; then
    echo "[1/3] ESP-IDF already cloned at $IDF_PATH, skipping."
else
    echo "[1/3] Cloning ESP-IDF $IDF_VER (shallow clone)..."
    mkdir -p "$(dirname "$IDF_PATH")"
    git clone -b "$IDF_VER" --depth 1 \
        https://github.com/espressif/esp-idf.git "$IDF_PATH"
    echo "   Done."
fi

# ---- 2. Install tools (cmake, ninja, toolchain, etc.) ----
echo "[2/3] Installing ESP-IDF tools for $IDF_TARGET..."
cd "$IDF_PATH"
./install.sh --shallow-clone "$IDF_TARGET"
echo "   Done."

# ---- 3. Add alias to shell config ----
SHELL_RC=""
case "$SHELL" in
    */bash) SHELL_RC="$HOME/.bashrc" ;;
    */zsh)  SHELL_RC="$HOME/.zshrc"  ;;
    */fish) SHELL_RC="$HOME/.config/fish/config.fish" ;;
    *)      SHELL_RC="$HOME/.bashrc" ;;
esac

ALIAS_LINE="alias get_idf='source $IDF_PATH/export.sh'"
if grep -q "get_idf" "$SHELL_RC" 2>/dev/null; then
    echo "[3/3] Alias 'get_idf' already in $SHELL_RC, skipping."
else
    echo "" >> "$SHELL_RC"
    echo "# ESP-IDF" >> "$SHELL_RC"
    echo "$ALIAS_LINE" >> "$SHELL_RC"
    echo "[3/3] Added alias to $SHELL_RC"
fi

echo ""
echo "=========================================="
echo " Install complete!"
echo "=========================================="
echo ""
echo "To use ESP-IDF in a new terminal:"
echo "  get_idf"
echo ""
echo "Then build the HHKB bridge:"
echo "  cd $HOME/hhkbridge/firmware"
echo "  idf.py build"
echo ""

# ---- Optional: USB permissions note ----
if ! groups | grep -q uucp; then
    echo "NOTE: For USB flashing, you need to be in the 'uucp' group."
    echo "Run:  sudo usermod -aG uucp \$USER"
    echo "Then log out and back in."
    echo ""
fi
