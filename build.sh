
#!/bin/sh
set -e

echo "=== ElDinWM Build Script (Pure C Version) ==="
echo ""

# Detect OS
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS_ID="$ID"
else
    OS_ID=$(uname -s | tr '[:upper:]' '[:lower:]')
fi

echo "Detected OS: $OS_ID"
echo ""

echo "Required dependencies:"
echo "  - wlroots (>= 0.17)"
echo "  - wayland, wayland-protocols"
echo "  - libxkbcommon, libinput, pixman"
echo "  - mesa/libdrm/gbm, seatd"
echo ""

case "$OS_ID" in
    ubuntu|debian)
        echo "Install dependencies (Debian/Ubuntu):"
        echo "  sudo apt update"
        echo "  sudo apt install -y build-essential pkg-config wayland-scanner"
        echo "  sudo apt install -y libwlroots-dev wayland-protocols libwayland-dev"
        echo "  sudo apt install -y libxkbcommon-dev libinput-dev libpixman-1-dev"
        echo "  sudo apt install -y libdrm-dev libgbm-dev libegl1-mesa-dev seatd libseat-dev"
        ;;
    arch|manjaro)
        echo "Install dependencies (Arch Linux):"
        echo "  sudo pacman -Syu --needed base-devel pkg-config"
        echo "  sudo pacman -S --needed wlroots wayland wayland-protocols"
        echo "  sudo pacman -S --needed libxkbcommon libinput pixman mesa seatd"
        ;;
    fedora|rhel|centos|rocky|almalinux)
        echo "Install dependencies (Fedora/RHEL/Rocky/CentOS):"
        echo "  sudo dnf groupinstall -y 'Development Tools'"
        echo "  sudo dnf install -y pkg-config wayland-devel wayland-scanner"
        echo "  sudo dnf install -y wlroots-devel wayland-protocols-devel"
        echo "  sudo dnf install -y libxkbcommon-devel libinput-devel pixman-devel"
        echo "  sudo dnf install -y mesa-libEGL-devel mesa-libgbm-devel libdrm-devel"
        echo "  sudo dnf install -y seatd seatd-devel"
        ;;
    opensuse*)
        echo "Install dependencies (openSUSE):"
        echo "  sudo zypper install -y -t pattern devel_basis"
        echo "  sudo zypper install -y pkg-config wlroots-devel wayland-devel"
        echo "  sudo zypper install -y wayland-protocols-devel libxkbcommon-devel"
        echo "  sudo zypper install -y libinput-devel libpixman-1-0-devel seatd"
        echo "  sudo zypper install -y Mesa-libEGL-devel Mesa-libgbm-devel libdrm-devel"
        ;;
    void)
        echo "Install dependencies (Void Linux):"
        echo "  sudo xbps-install -Syu base-devel pkg-config"
        echo "  sudo xbps-install -y wlroots-devel wayland-devel wayland-protocols"
        echo "  sudo xbps-install -y libxkbcommon-devel libinput-devel pixman-devel"
        echo "  sudo xbps-install -y mesa-devel seatd seatd-devel"
        ;;
    *)
        echo "Please install: wlroots, wayland, wayland-protocols, libxkbcommon,"
        echo "libinput, pixman, mesa/libdrm/gbm, and seatd manually"
        ;;
esac

echo ""
echo "=== Checking Tools ==="

if ! command -v pkg-config >/dev/null 2>&1; then
    echo "Error: pkg-config not found"
    exit 1
fi

if ! command -v wayland-scanner >/dev/null 2>&1; then
    echo "Error: wayland-scanner not found"
    echo "Install: wayland-scanner (Debian/Ubuntu: apt install wayland-protocols)"
    exit 1
fi

# Try to find the right wlroots version
WLROOTS_PKG=""
for ver in wlroots-0.18 wlroots-0.17 wlroots; do
    if pkg-config --exists "$ver" 2>/dev/null; then
        WLROOTS_PKG="$ver"
        echo "Found: $ver ($(pkg-config --modversion $ver))"
        break
    fi
done

if [ -z "$WLROOTS_PKG" ]; then
    echo "Error: wlroots not found"
    exit 1
fi

echo "Checking dependencies..."
for lib in "$WLROOTS_PKG" wayland-server xkbcommon libinput pixman-1; do
    if ! pkg-config --exists "$lib" 2>/dev/null; then
        echo "Error: $lib not found"
        exit 1
    fi
done

echo ""
echo "=== Generating Protocol Headers ==="

PROTO_DIR=$(pkg-config --variable=pkgdatadir wayland-protocols)
if [ -z "$PROTO_DIR" ]; then
    echo "Error: Cannot find wayland-protocols directory"
    exit 1
fi

echo "Protocol directory: $PROTO_DIR"

# Generate xdg-shell protocol
if [ ! -f xdg-shell-protocol.h ] || [ ! -f xdg-shell-protocol.c ]; then
    echo "Generating xdg-shell protocol..."

    wayland-scanner server-header \
        "$PROTO_DIR/stable/xdg-shell/xdg-shell.xml" \
        xdg-shell-protocol.h

    wayland-scanner private-code \
        "$PROTO_DIR/stable/xdg-shell/xdg-shell.xml" \
        xdg-shell-protocol.c

    echo "✓ Generated xdg-shell-protocol.h"
    echo "✓ Generated xdg-shell-protocol.c"
else
    echo "✓ Protocol headers already exist"
fi

echo ""
echo "=== Building ElDinWM ==="

gcc -std=c11 -O2 -o eldinwm eldinwm.c xdg-shell-protocol.c \
    -I. \
    $(pkg-config --cflags --libs "$WLROOTS_PKG" wayland-server xkbcommon libinput pixman-1) \
    -DWLR_USE_UNSTABLE

if [ $? -eq 0 ]; then
    echo ""
    echo "=== Build Successful ==="
    echo "Binary: ./eldinwm"
    echo ""
    echo "Create config at: ~/.config/eldinwm/eldinwm.conf"
    echo "To run: ./eldinwm"
else
    echo ""
    echo "Build failed"
    exit 1
fi
