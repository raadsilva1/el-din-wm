#!/bin/sh
set -e

# ElDinWM Build Script
# Detects OS and provides dependency installation instructions

echo "=== ElDinWM Build Script ==="
echo ""

# Detect OS
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS_ID="$ID"
    OS_ID_LIKE="$ID_LIKE"
else
    # BSD systems
    OS_ID=$(uname -s | tr '[:upper:]' '[:lower:]')
fi

echo "Detected OS: $OS_ID"
echo ""

# Dependency list
echo "Required dependencies:"
echo "  - wlroots (>= 0.17)"
echo "  - wayland"
echo "  - wayland-protocols"
echo "  - libxkbcommon"
echo "  - libinput"
echo "  - pixman"
echo "  - mesa / libdrm / gbm"
echo "  - seat management (seatd, elogind, or systemd-logind)"
echo ""

# Provide install instructions based on distro
case "$OS_ID" in
    ubuntu|debian)
        echo "Install dependencies (Ubuntu/Debian):"
        echo "  sudo apt update"
        echo "  sudo apt install -y build-essential pkg-config meson ninja-build"
        echo "  sudo apt install -y libwlroots-dev wayland-protocols libwayland-dev"
        echo "  sudo apt install -y libxkbcommon-dev libinput-dev libpixman-1-dev"
        echo "  sudo apt install -y libdrm-dev libgbm-dev libegl1-mesa-dev"
        echo "  sudo apt install -y seatd libseat-dev"
        ;;
    arch|manjaro)
        echo "Install dependencies (Arch Linux):"
        echo "  sudo pacman -Syu --needed base-devel pkg-config meson ninja"
        echo "  sudo pacman -S --needed wlroots wayland wayland-protocols"
        echo "  sudo pacman -S --needed libxkbcommon libinput pixman mesa seatd"
        ;;
    fedora|rhel|centos|rocky|almalinux|openeuler)
        echo "Install dependencies (Fedora/RHEL/Rocky/AlmaLinux/openEuler):"
        echo "  sudo dnf groupinstall -y 'Development Tools'"
        echo "  sudo dnf install -y pkg-config meson ninja-build"
        echo "  sudo dnf install -y wlroots-devel wayland-devel wayland-protocols-devel"
        echo "  sudo dnf install -y libxkbcommon-devel libinput-devel pixman-devel"
        echo "  sudo dnf install -y mesa-libEGL-devel mesa-libgbm-devel libdrm-devel"
        echo "  sudo dnf install -y seatd seatd-devel"
        ;;
    opensuse*|sles)
        echo "Install dependencies (openSUSE):"
        echo "  sudo zypper install -y -t pattern devel_basis"
        echo "  sudo zypper install -y pkg-config meson ninja"
        echo "  sudo zypper install -y wlroots-devel wayland-devel wayland-protocols-devel"
        echo "  sudo zypper install -y libxkbcommon-devel libinput-devel libpixman-1-0-devel"
        echo "  sudo zypper install -y Mesa-libEGL-devel Mesa-libgbm-devel libdrm-devel"
        echo "  sudo zypper install -y seatd"
        ;;
    void)
        echo "Install dependencies (Void Linux):"
        echo "  sudo xbps-install -Syu xbps"
        echo "  sudo xbps-install -y base-devel pkg-config meson ninja"
        echo "  sudo xbps-install -y wlroots-devel wayland-devel wayland-protocols"
        echo "  sudo xbps-install -y libxkbcommon-devel libinput-devel pixman-devel"
        echo "  sudo xbps-install -y mesa-devel seatd seatd-devel"
        ;;
    openbsd)
        echo "Install dependencies (OpenBSD):"
        echo "  Use pkg_add or build from ports:"
        echo "  doas pkg_add wlroots wayland wayland-protocols libxkbcommon libinput pixman"
        echo ""
        echo "  Note: Check /usr/ports for latest package names"
        ;;
    netbsd)
        echo "Install dependencies (NetBSD):"
        echo "  Use pkgin or build from pkgsrc:"
        echo "  pkgin install wlroots wayland wayland-protocols libxkbcommon libinput pixman"
        echo ""
        echo "  Note: Check pkgsrc for latest package names"
        ;;
    freebsd)
        echo "Install dependencies (FreeBSD):"
        echo "  pkg install wlroots wayland wayland-protocols libxkbcommon libinput pixman seatd"
        ;;
    *)
        echo "Unknown distribution. Please install the following manually:"
        echo "  wlroots, wayland, wayland-protocols, libxkbcommon, libinput, pixman,"
        echo "  mesa/libdrm/gbm, and seatd (or elogind/systemd-logind)"
        ;;
esac

echo ""
echo "=== Building ElDinWM ==="

# Check for required tools
if ! command -v pkg-config >/dev/null 2>&1; then
    echo "Error: pkg-config not found. Please install it first."
    exit 1
fi

# Check for required libraries
echo "Checking for required libraries..."
for lib in wlroots wayland-server xkbcommon libinput pixman-1; do
    if ! pkg-config --exists "$lib" 2>/dev/null; then
        echo "Error: $lib not found. Please install dependencies first."
        exit 1
    fi
done

echo "All dependencies found!"
echo ""

# Build with pkg-config
echo "Building with g++..."
g++ -std=c++23 -O2 -o eldinwm eldinwm.cpp \
    $(pkg-config --cflags --libs wlroots wayland-server xkbcommon libinput pixman-1) \
    -DWLR_USE_UNSTABLE

if [ $? -eq 0 ]; then
    echo ""
    echo "=== Build Successful ==="
    echo "Binary: ./eldinwm"
    echo ""
    echo "To run: ./eldinwm"
    echo "Config file location (create one of these):"
    echo "  \$XDG_CONFIG_HOME/eldinwm/eldinwm.conf"
    echo "  ~/.config/eldinwm/eldinwm.conf"
    echo "  /etc/eldinwm/eldinwm.conf"
else
    echo ""
    echo "Build failed. Please check error messages above."
    exit 1
fi
