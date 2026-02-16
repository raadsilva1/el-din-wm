# Find the XML file
PROTO_DIR=$(pkg-config --variable=pkgdatadir wayland-protocols)/stable/xdg-shell

# Generate the header
wayland-scanner server-header \
    $PROTO_DIR/xdg-shell.xml \
    xdg-shell-protocol.h

# Compile with current directory in include path
g++ -std=c++23 -O2 -o eldinwm eldinwm.cpp \
    -I. \
    $(pkg-config --cflags --libs wlroots-0.18 wayland-server xkbcommon libinput pixman-1) \
    -DWLR_USE_UNSTABLE
