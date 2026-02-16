# ElDinWM

**ElDinWM** is a minimalist Wayland tiling compositor/window manager written in modern C++23 with wlroots. It provides a simple, keyboard-driven tiling workflow with a maximum of 2 windows per workspace.

## Features

- **Tiling Layout**: Maximum 2 windows per workspace
  - 1 window: fullscreen
  - 2 windows: 50/50 horizontal split (left/right)
- **Multiple Workspaces**: Configurable number (default: 4)
- **Always-visible Workspace Indicator**: Centered at top of screen
- **Built-in Command Launcher**: Execute shell commands without external launcher
- **Keyboard-driven**: All operations via keyboard shortcuts
- **Mouse Input**: Delivered to windows for normal interaction (no click-to-focus or drag-to-move)
- **Multi-output Support**: Same workspace logic applied per output
- **Background Images**: Optional fullscreen background image per workspace

## Build Requirements

### Dependencies

- **wlroots** >= 0.17
- **wayland** and **wayland-protocols**
- **libxkbcommon**
- **libinput**
- **pixman**
- **mesa** / **libdrm** / **gbm**
- **seatd** (or elogind/systemd-logind)
- **C++23 compiler** (g++ >= 13 or clang++ >= 16)

### Installation Instructions

#### Ubuntu / Debian
```bash
sudo apt update
sudo apt install -y build-essential pkg-config meson ninja-build
sudo apt install -y libwlroots-dev wayland-protocols libwayland-dev
sudo apt install -y libxkbcommon-dev libinput-dev libpixman-1-dev
sudo apt install -y libdrm-dev libgbm-dev libegl1-mesa-dev
sudo apt install -y seatd libseat-dev
