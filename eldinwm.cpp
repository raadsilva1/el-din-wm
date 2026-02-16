/*
 * ElDinWM - Minimalist Wayland Tiling Compositor
 */

extern "C" {
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
}

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <csignal>
#include <sys/wait.h>
#include <pwd.h>

// Embedded simple bitmap font (7x9 pixels per char, ASCII 32-126)
// This is a minimal implementation to avoid external dependencies
namespace Font {
    constexpr int CHAR_WIDTH = 7;
    constexpr int CHAR_HEIGHT = 9;
    constexpr int FIRST_CHAR = 32;
    constexpr int LAST_CHAR = 126;
    
    // Simplified bitmap font data (each char is 9 bytes, 7 bits used per byte)
    // Format: each byte represents one row of pixels (7 bits wide)
    constexpr uint8_t FONT_DATA[][9] = {
        // Space (32)
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        // ! (33)
        {0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x08, 0x00, 0x00},
        // " (34)
        {0x14, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        // # (35)
        {0x14, 0x14, 0x3E, 0x14, 0x3E, 0x14, 0x14, 0x00, 0x00},
        // $ (36)
        {0x08, 0x1E, 0x28, 0x1C, 0x0A, 0x3C, 0x08, 0x00, 0x00},
        // % (37)
        {0x30, 0x32, 0x04, 0x08, 0x10, 0x26, 0x06, 0x00, 0x00},
        // & (38)
        {0x10, 0x28, 0x28, 0x10, 0x2A, 0x24, 0x1A, 0x00, 0x00},
        // ' (39)
        {0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        // ( (40)
        {0x04, 0x08, 0x10, 0x10, 0x10, 0x08, 0x04, 0x00, 0x00},
        // ) (41)
        {0x10, 0x08, 0x04, 0x04, 0x04, 0x08, 0x10, 0x00, 0x00},
        // * (42)
        {0x00, 0x08, 0x2A, 0x1C, 0x2A, 0x08, 0x00, 0x00, 0x00},
        // + (43)
        {0x00, 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, 0x00, 0x00},
        // , (44)
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x10, 0x00},
        // - (45)
        {0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00},
        // . (46)
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00},
        // / (47)
        {0x00, 0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x00, 0x00},
        // 0 (48)
        {0x1C, 0x22, 0x26, 0x2A, 0x32, 0x22, 0x1C, 0x00, 0x00},
        // 1 (49)
        {0x08, 0x18, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00},
        // 2 (50)
        {0x1C, 0x22, 0x02, 0x0C, 0x10, 0x20, 0x3E, 0x00, 0x00},
        // 3 (51)
        {0x1C, 0x22, 0x02, 0x0C, 0x02, 0x22, 0x1C, 0x00, 0x00},
        // 4 (52)
        {0x04, 0x0C, 0x14, 0x24, 0x3E, 0x04, 0x04, 0x00, 0x00},
        // 5 (53)
        {0x3E, 0x20, 0x3C, 0x02, 0x02, 0x22, 0x1C, 0x00, 0x00},
        // 6 (54)
        {0x0C, 0x10, 0x20, 0x3C, 0x22, 0x22, 0x1C, 0x00, 0x00},
        // 7 (55)
        {0x3E, 0x02, 0x04, 0x08, 0x10, 0x10, 0x10, 0x00, 0x00},
        // 8 (56)
        {0x1C, 0x22, 0x22, 0x1C, 0x22, 0x22, 0x1C, 0x00, 0x00},
        // 9 (57)
        {0x1C, 0x22, 0x22, 0x1E, 0x02, 0x04, 0x18, 0x00, 0x00},
        // : (58)
        {0x00, 0x00, 0x08, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00},
        // ; (59)
        {0x00, 0x00, 0x08, 0x00, 0x00, 0x08, 0x08, 0x10, 0x00},
        // < (60)
        {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02, 0x00, 0x00},
        // = (61)
        {0x00, 0x00, 0x3E, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00},
        // > (62)
        {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00},
        // ? (63)
        {0x1C, 0x22, 0x04, 0x08, 0x08, 0x00, 0x08, 0x00, 0x00},
        // @ (64)
        {0x1C, 0x22, 0x2E, 0x2A, 0x2E, 0x20, 0x1C, 0x00, 0x00},
        // A (65)
        {0x08, 0x14, 0x22, 0x22, 0x3E, 0x22, 0x22, 0x00, 0x00},
        // B (66)
        {0x3C, 0x22, 0x22, 0x3C, 0x22, 0x22, 0x3C, 0x00, 0x00},
        // C (67)
        {0x1C, 0x22, 0x20, 0x20, 0x20, 0x22, 0x1C, 0x00, 0x00},
        // D (68)
        {0x38, 0x24, 0x22, 0x22, 0x22, 0x24, 0x38, 0x00, 0x00},
        // E (69)
        {0x3E, 0x20, 0x20, 0x3C, 0x20, 0x20, 0x3E, 0x00, 0x00},
        // F (70)
        {0x3E, 0x20, 0x20, 0x3C, 0x20, 0x20, 0x20, 0x00, 0x00},
        // G (71)
        {0x1C, 0x22, 0x20, 0x2E, 0x22, 0x22, 0x1C, 0x00, 0x00},
        // H (72)
        {0x22, 0x22, 0x22, 0x3E, 0x22, 0x22, 0x22, 0x00, 0x00},
        // I (73)
        {0x1C, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00},
        // J (74)
        {0x0E, 0x04, 0x04, 0x04, 0x04, 0x24, 0x18, 0x00, 0x00},
        // K (75)
        {0x22, 0x24, 0x28, 0x30, 0x28, 0x24, 0x22, 0x00, 0x00},
        // L (76)
        {0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x3E, 0x00, 0x00},
        // M (77)
        {0x22, 0x36, 0x2A, 0x2A, 0x22, 0x22, 0x22, 0x00, 0x00},
        // N (78)
        {0x22, 0x32, 0x2A, 0x26, 0x22, 0x22, 0x22, 0x00, 0x00},
        // O (79)
        {0x1C, 0x22, 0x22, 0x22, 0x22, 0x22, 0x1C, 0x00, 0x00},
        // P (80)
        {0x3C, 0x22, 0x22, 0x3C, 0x20, 0x20, 0x20, 0x00, 0x00},
        // Q (81)
        {0x1C, 0x22, 0x22, 0x22, 0x2A, 0x24, 0x1A, 0x00, 0x00},
        // R (82)
        {0x3C, 0x22, 0x22, 0x3C, 0x28, 0x24, 0x22, 0x00, 0x00},
        // S (83)
        {0x1C, 0x22, 0x20, 0x1C, 0x02, 0x22, 0x1C, 0x00, 0x00},
        // T (84)
        {0x3E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00},
        // U (85)
        {0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x1C, 0x00, 0x00},
        // V (86)
        {0x22, 0x22, 0x22, 0x22, 0x14, 0x14, 0x08, 0x00, 0x00},
        // W (87)
        {0x22, 0x22, 0x22, 0x2A, 0x2A, 0x36, 0x22, 0x00, 0x00},
        // X (88)
        {0x22, 0x22, 0x14, 0x08, 0x14, 0x22, 0x22, 0x00, 0x00},
        // Y (89)
        {0x22, 0x22, 0x14, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00},
        // Z (90)
        {0x3E, 0x02, 0x04, 0x08, 0x10, 0x20, 0x3E, 0x00, 0x00},
        // [ (91)
        {0x1C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1C, 0x00, 0x00},
        // \ (92)
        {0x00, 0x20, 0x10, 0x08, 0x04, 0x02, 0x00, 0x00, 0x00},
        // ] (93)
        {0x1C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1C, 0x00, 0x00},
        // ^ (94)
        {0x08, 0x14, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        // _ (95)
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x00},
        // ` (96)
        {0x10, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        // a (97)
        {0x00, 0x00, 0x1C, 0x02, 0x1E, 0x22, 0x1E, 0x00, 0x00},
        // b (98)
        {0x20, 0x20, 0x3C, 0x22, 0x22, 0x22, 0x3C, 0x00, 0x00},
        // c (99)
        {0x00, 0x00, 0x1C, 0x22, 0x20, 0x22, 0x1C, 0x00, 0x00},
        // d (100)
        {0x02, 0x02, 0x1E, 0x22, 0x22, 0x22, 0x1E, 0x00, 0x00},
        // e (101)
        {0x00, 0x00, 0x1C, 0x22, 0x3E, 0x20, 0x1C, 0x00, 0x00},
        // f (102)
        {0x0C, 0x12, 0x10, 0x3C, 0x10, 0x10, 0x10, 0x00, 0x00},
        // g (103)
        {0x00, 0x00, 0x1E, 0x22, 0x22, 0x1E, 0x02, 0x1C, 0x00},
        // h (104)
        {0x20, 0x20, 0x3C, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00},
        // i (105)
        {0x08, 0x00, 0x18, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00},
        // j (106)
        {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x24, 0x18, 0x00},
        // k (107)
        {0x20, 0x20, 0x22, 0x24, 0x38, 0x24, 0x22, 0x00, 0x00},
        // l (108)
        {0x18, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00},
        // m (109)
        {0x00, 0x00, 0x36, 0x2A, 0x2A, 0x2A, 0x22, 0x00, 0x00},
        // n (110)
        {0x00, 0x00, 0x3C, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00},
        // o (111)
        {0x00, 0x00, 0x1C, 0x22, 0x22, 0x22, 0x1C, 0x00, 0x00},
        // p (112)
        {0x00, 0x00, 0x3C, 0x22, 0x22, 0x3C, 0x20, 0x20, 0x00},
        // q (113)
        {0x00, 0x00, 0x1E, 0x22, 0x22, 0x1E, 0x02, 0x02, 0x00},
        // r (114)
        {0x00, 0x00, 0x2C, 0x32, 0x20, 0x20, 0x20, 0x00, 0x00},
        // s (115)
        {0x00, 0x00, 0x1E, 0x20, 0x1C, 0x02, 0x3C, 0x00, 0x00},
        // t (116)
        {0x10, 0x10, 0x3C, 0x10, 0x10, 0x12, 0x0C, 0x00, 0x00},
        // u (117)
        {0x00, 0x00, 0x22, 0x22, 0x22, 0x26, 0x1A, 0x00, 0x00},
        // v (118)
        {0x00, 0x00, 0x22, 0x22, 0x22, 0x14, 0x08, 0x00, 0x00},
        // w (119)
        {0x00, 0x00, 0x22, 0x22, 0x2A, 0x2A, 0x14, 0x00, 0x00},
        // x (120)
        {0x00, 0x00, 0x22, 0x14, 0x08, 0x14, 0x22, 0x00, 0x00},
        // y (121)
        {0x00, 0x00, 0x22, 0x22, 0x22, 0x1E, 0x02, 0x1C, 0x00},
        // z (122)
        {0x00, 0x00, 0x3E, 0x04, 0x08, 0x10, 0x3E, 0x00, 0x00},
        // { (123)
        {0x0E, 0x08, 0x08, 0x30, 0x08, 0x08, 0x0E, 0x00, 0x00},
        // | (124)
        {0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00},
        // } (125)
        {0x38, 0x08, 0x08, 0x06, 0x08, 0x08, 0x38, 0x00, 0x00},
        // ~ (126)
        {0x00, 0x00, 0x00, 0x32, 0x4C, 0x00, 0x00, 0x00, 0x00},
    };
    
    void draw_char(uint32_t* buffer, int buf_width, int buf_height, 
                   char c, int x, int y, uint32_t color) {
        if (c < FIRST_CHAR || c > LAST_CHAR) return;
        const uint8_t* glyph = FONT_DATA[c - FIRST_CHAR];
        
        for (int row = 0; row < CHAR_HEIGHT; ++row) {
            for (int col = 0; col < CHAR_WIDTH; ++col) {
                if (glyph[row] & (1 << (6 - col))) {
                    int px = x + col;
                    int py = y + row;
                    if (px >= 0 && px < buf_width && py >= 0 && py < buf_height) {
                        buffer[py * buf_width + px] = color;
                    }
                }
            }
        }
    }
    
    void draw_string(uint32_t* buffer, int buf_width, int buf_height,
                     const std::string& text, int x, int y, uint32_t color) {
        int cursor_x = x;
        for (char c : text) {
            draw_char(buffer, buf_width, buf_height, c, cursor_x, y, color);
            cursor_x += CHAR_WIDTH + 1;
        }
    }
}

// Configuration
struct Config {
    int workspaces = 4;
    std::string background_image;
};

// Forward declarations
struct Server;
struct Output;
struct View;

// View represents a window
struct View {
    Server* server;
    wlr_xdg_toplevel* xdg_toplevel;
    wlr_scene_tree* scene_tree;
    
    wl_listener map;
    wl_listener unmap;
    wl_listener destroy;
    wl_listener request_move;
    wl_listener request_resize;
    wl_listener request_maximize;
    wl_listener request_fullscreen;
    
    int workspace_idx = 0;
    bool mapped = false;
};

// Workspace holds up to 2 views
struct Workspace {
    std::vector<View*> views;  // Max 2
};

// Output represents a monitor
struct Output {
    Server* server;
    wlr_output* wlr_output;
    wl_listener frame;
    wl_listener destroy;
    wlr_scene_output* scene_output;
    
    int current_workspace = 0;
    std::vector<Workspace> workspaces;
    uint64_t workspace_switch_time = 0; // For highlight animation
};

// Command box state
struct CommandBox {
    bool active = false;
    std::string text;
};

// Server global state
struct Server {
    wl_display* wl_display = nullptr;
    wlr_backend* backend = nullptr;
    wlr_renderer* renderer = nullptr;
    wlr_allocator* allocator = nullptr;
    wlr_scene* scene = nullptr;
    wlr_scene_output_layout* scene_layout = nullptr;
    
    wlr_xdg_shell* xdg_shell = nullptr;
    wlr_cursor* cursor = nullptr;
    wlr_xcursor_manager* cursor_mgr = nullptr;
    wlr_seat* seat = nullptr;
    wlr_output_layout* output_layout = nullptr;
    
    std::vector<std::unique_ptr<View>> views;
    std::vector<std::unique_ptr<Output>> outputs;
    
    wl_listener new_output;
    wl_listener new_xdg_surface;
    wl_listener cursor_motion;
    wl_listener cursor_motion_absolute;
    wl_listener cursor_button;
    wl_listener cursor_axis;
    wl_listener cursor_frame;
    wl_listener new_input;
    wl_listener request_cursor;
    wl_listener request_set_selection;
    
    std::vector<wlr_keyboard*> keyboards;
    
    Config config;
    CommandBox command_box;
    bool running = true;
    
    // Background color (dark blue)
    float bg_color[4] = {0.0f, 0.05f, 0.15f, 1.0f};
};

// Helper: get workspace-specific scene tree
wlr_scene_tree* get_workspace_tree(Output* output, int ws_idx) {
    // For simplicity, we use visibility toggling on scene nodes
    // In production, you'd create per-workspace trees
    return output->server->scene->tree;
}

// Layout computation: place windows
void relayout_workspace(Output* output) {
    if (!output || !output->wlr_output) return;
    
    Workspace& ws = output->workspaces[output->current_workspace];
    int width = output->wlr_output->width;
    int height = output->wlr_output->height;
    
    constexpr int INDICATOR_HEIGHT = 28;
    int usable_height = height - INDICATOR_HEIGHT;
    
    // Hide all views first
    for (auto& view_ptr : output->server->views) {
        if (view_ptr->mapped) {
            wlr_scene_node_set_enabled(&view_ptr->scene_tree->node, false);
        }
    }
    
    // Show and position views in current workspace
    if (ws.views.size() == 1) {
        // Fullscreen
        View* v = ws.views[0];
        if (v->mapped) {
            wlr_scene_node_set_enabled(&v->scene_tree->node, true);
            wlr_scene_node_set_position(&v->scene_tree->node, 0, INDICATOR_HEIGHT);
            wlr_xdg_toplevel_set_size(v->xdg_toplevel, width, usable_height);
        }
    } else if (ws.views.size() == 2) {
        // 50/50 split (left/right)
        int half_width = width / 2;
        for (size_t i = 0; i < 2; ++i) {
            View* v = ws.views[i];
            if (v->mapped) {
                wlr_scene_node_set_enabled(&v->scene_tree->node, true);
                wlr_scene_node_set_position(&v->scene_tree->node, 
                    i == 0 ? 0 : half_width, INDICATOR_HEIGHT);
                wlr_xdg_toplevel_set_size(v->xdg_toplevel, half_width, usable_height);
            }
        }
    }
}

// Find next available workspace slot across all outputs
std::optional<std::pair<Output*, int>> find_available_workspace(Server* server) {
    for (auto& output : server->outputs) {
        for (size_t i = 0; i < output->workspaces.size(); ++i) {
            if (output->workspaces[i].views.size() < 2) {
                return std::make_pair(output.get(), static_cast<int>(i));
            }
        }
    }
    return std::nullopt;
}

// Add view to workspace
void add_view_to_workspace(Output* output, int ws_idx, View* view) {
    output->workspaces[ws_idx].views.push_back(view);
    view->workspace_idx = ws_idx;
}

// Remove view from workspace
void remove_view_from_workspace(View* view) {
    for (auto& output : view->server->outputs) {
        for (auto& ws : output->workspaces) {
            auto it = std::find(ws.views.begin(), ws.views.end(), view);
            if (it != ws.views.end()) {
                ws.views.erase(it);
                relayout_workspace(output.get());
                return;
            }
        }
    }
}

// Execute shell command
void execute_command(const std::string& cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        setsid();
        execl("/bin/sh", "/bin/sh", "-lc", cmd.c_str(), nullptr);
        _exit(1);
    } else if (pid > 0) {
        // Parent: reap child asynchronously
        signal(SIGCHLD, SIG_IGN);
    }
}

// Keyboard handling
void handle_keybinding(Server* server, xkb_keysym_t sym, uint32_t modifiers) {
    constexpr uint32_t CTRL_SHIFT = WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT;
    
    if (server->command_box.active) {
        // Command box is open: handle input
        if (sym == XKB_KEY_Escape) {
            server->command_box.active = false;
            server->command_box.text.clear();
        } else if (sym == XKB_KEY_Return) {
            if (!server->command_box.text.empty()) {
                execute_command(server->command_box.text);
            }
            server->command_box.active = false;
            server->command_box.text.clear();
        } else if (sym == XKB_KEY_BackSpace) {
            if (!server->command_box.text.empty()) {
                server->command_box.text.pop_back();
            }
        } else if (sym >= 32 && sym < 127) {
            // ASCII printable
            server->command_box.text += static_cast<char>(sym);
        }
        
        // Force redraw
        for (auto& output : server->outputs) {
            wlr_output_schedule_frame(output->wlr_output);
        }
        return;
    }
    
    // Global shortcuts
    if (modifiers == CTRL_SHIFT) {
        switch (sym) {
            case XKB_KEY_Down:
                // Exit
                wl_display_terminate(server->wl_display);
                server->running = false;
                break;
                
            case XKB_KEY_Left:
            case XKB_KEY_Right:
                // Switch workspace
                for (auto& output : server->outputs) {
                    int delta = (sym == XKB_KEY_Right) ? 1 : -1;
                    int new_ws = output->current_workspace + delta;
                    if (new_ws >= 0 && new_ws < static_cast<int>(output->workspaces.size())) {
                        output->current_workspace = new_ws;
                        output->workspace_switch_time = 
                            static_cast<uint64_t>(wl_display_get_event_loop(server->wl_display)->timer);
                        relayout_workspace(output.get());
                        wlr_output_schedule_frame(output->wlr_output);
                    }
                }
                break;
                
            case XKB_KEY_z:
            case XKB_KEY_Z:
                // Open command box
                server->command_box.active = true;
                server->command_box.text.clear();
                for (auto& output : server->outputs) {
                    wlr_output_schedule_frame(output->wlr_output);
                }
                break;
                
            case XKB_KEY_x:
            case XKB_KEY_X:
                // Cycle focus in current workspace
                for (auto& output : server->outputs) {
                    Workspace& ws = output->workspaces[output->current_workspace];
                    if (ws.views.size() == 2) {
                        // Find currently focused, switch to other
                        wlr_surface* focused = server->seat->keyboard_state.focused_surface;
                        View* other = nullptr;
                        for (View* v : ws.views) {
                            if (v->xdg_toplevel->base->surface != focused) {
                                other = v;
                                break;
                            }
                        }
                        if (other && other->mapped) {
                            wlr_seat_keyboard_notify_enter(server->seat,
                                other->xdg_toplevel->base->surface,
                                nullptr, 0, nullptr);
                        }
                    }
                }
                break;
        }
    }
}

void keyboard_handle_modifiers(wl_listener* listener, void* data) {
    auto* kb = reinterpret_cast<wlr_keyboard*>(
        reinterpret_cast<char*>(listener) - offsetof(wlr_keyboard, events.modifiers));
    wlr_seat_set_keyboard(static_cast<Server*>(kb->data)->seat, kb);
    wlr_seat_keyboard_notify_modifiers(static_cast<Server*>(kb->data)->seat, &kb->modifiers);
}

void keyboard_handle_key(wl_listener* listener, void* data) {
    auto* event = static_cast<wlr_keyboard_key_event*>(data);
    auto* kb = reinterpret_cast<wlr_keyboard*>(
        reinterpret_cast<char*>(listener) - offsetof(wlr_keyboard, events.key));
    auto* server = static_cast<Server*>(kb->data);
    
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        uint32_t keycode = event->keycode + 8;
        const xkb_keysym_t* syms;
        int nsyms = xkb_state_key_get_syms(kb->xkb_state, keycode, &syms);
        
        uint32_t modifiers = wlr_keyboard_get_modifiers(kb);
        
        for (int i = 0; i < nsyms; ++i) {
            handle_keybinding(server, syms[i], modifiers);
        }
    }
    
    wlr_seat_set_keyboard(server->seat, kb);
    wlr_seat_keyboard_notify_key(server->seat, event->time_msec,
        event->keycode, event->state);
}

void server_new_keyboard(Server* server, wlr_input_device* device) {
    auto* kb = wlr_keyboard_from_input_device(device);
    kb->data = server;
    
    xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_keymap* keymap = xkb_keymap_new_from_names(context, nullptr,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    
    wlr_keyboard_set_keymap(kb, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(kb, 25, 600);
    
    static wl_listener modifiers_listener;
    static wl_listener key_listener;
    
    modifiers_listener.notify = keyboard_handle_modifiers;
    key_listener.notify = keyboard_handle_key;
    
    wl_signal_add(&kb->events.modifiers, &modifiers_listener);
    wl_signal_add(&kb->events.key, &key_listener);
    
    wlr_seat_set_keyboard(server->seat, kb);
    server->keyboards.push_back(kb);
}

void server_new_pointer(Server* server, wlr_input_device* device) {
    wlr_cursor_attach_input_device(server->cursor, device);
}

void server_new_input(wl_listener* listener, void* data) {
    auto* server = wl_container_of(listener, server, new_input);
    auto* device = static_cast<wlr_input_device*>(data);
    
    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            server_new_keyboard(server, device);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            server_new_pointer(server, device);
            break;
        default:
            break;
    }
    
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(server->seat, caps);
}

// Cursor handling
void process_cursor_motion(Server* server, uint32_t time) {
    wlr_seat_pointer_notify_motion(server->seat, time,
        server->cursor->x, server->cursor->y);
}

void server_cursor_motion(wl_listener* listener, void* data) {
    auto* server = wl_container_of(listener, server, cursor_motion);
    auto* event = static_cast<wlr_pointer_motion_event*>(data);
    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    process_cursor_motion(server, event->time_msec);
}

void server_cursor_motion_absolute(wl_listener* listener, void* data) {
    auto* server = wl_container_of(listener, server, cursor_motion_absolute);
    auto* event = static_cast<wlr_pointer_motion_absolute_event*>(data);
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(server, event->time_msec);
}

void server_cursor_button(wl_listener* listener, void* data) {
    auto* server = wl_container_of(listener, server, cursor_button);
    auto* event = static_cast<wlr_pointer_button_event*>(data);
    wlr_seat_pointer_notify_button(server->seat, event->time_msec,
        event->button, event->state);
}

void server_cursor_axis(wl_listener* listener, void* data) {
    auto* server = wl_container_of(listener, server, cursor_axis);
    auto* event = static_cast<wlr_pointer_axis_event*>(data);
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
        event->orientation, event->delta, event->delta_discrete, event->source,
        event->relative_direction);
}

void server_cursor_frame(wl_listener* listener, void* data) {
    auto* server = wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}

void seat_request_cursor(wl_listener* listener, void* data) {
    auto* server = wl_container_of(listener, server, request_cursor);
    auto* event = static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);
    wlr_seat_client* focused_client = server->seat->pointer_state.focused_client;
    
    if (focused_client == event->seat_client) {
        wlr_cursor_set_surface(server->cursor, event->surface,
            event->hotspot_x, event->hotspot_y);
    }
}

void seat_request_set_selection(wl_listener* listener, void* data) {
    auto* server = wl_container_of(listener, server, request_set_selection);
    auto* event = static_cast<wlr_seat_request_set_selection_event*>(data);
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

// View event handlers
void xdg_toplevel_map(wl_listener* listener, void* data) {
    auto* view = wl_container_of(listener, view, map);
    view->mapped = true;
    
    // Find available workspace
    auto available = find_available_workspace(view->server);
    if (available) {
        add_view_to_workspace(available->first, available->second, view);
        relayout_workspace(available->first);
        
        // Focus the new window
        wlr_seat_keyboard_notify_enter(view->server->seat,
            view->xdg_toplevel->base->surface, nullptr, 0, nullptr);
    } else {
        // No space available: hide and show warning
        wlr_scene_node_set_enabled(&view->scene_tree->node, false);
        fprintf(stderr, "ElDinWM: Cannot map window - all workspaces full (max 2 per workspace)\n");
    }
}

void xdg_toplevel_unmap(wl_listener* listener, void* data) {
    auto* view = wl_container_of(listener, view, unmap);
    view->mapped = false;
    remove_view_from_workspace(view);
}

void xdg_toplevel_destroy(wl_listener* listener, void* data) {
    auto* view = wl_container_of(listener, view, destroy);
    
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->request_move.link);
    wl_list_remove(&view->request_resize.link);
    wl_list_remove(&view->request_maximize.link);
    wl_list_remove(&view->request_fullscreen.link);
    
    auto& views = view->server->views;
    views.erase(std::remove_if(views.begin(), views.end(),
        [view](auto& v) { return v.get() == view; }), views.end());
}

void xdg_toplevel_request_move(wl_listener* listener, void* data) {
    // No drag-to-move support
}

void xdg_toplevel_request_resize(wl_listener* listener, void* data) {
    // No manual resize support
}

void xdg_toplevel_request_maximize(wl_listener* listener, void* data) {
    auto* view = wl_container_of(listener, view, request_maximize);
    wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

void xdg_toplevel_request_fullscreen(wl_listener* listener, void* data) {
    auto* view = wl_container_of(listener, view, request_fullscreen);
    wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, false);
}

void server_new_xdg_surface(wl_listener* listener, void* data) {
    auto* server = wl_container_of(listener, server, new_xdg_surface);
    auto* xdg_surface = static_cast<wlr_xdg_surface*>(data);
    
    if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        return;
    }
    
    auto view = std::make_unique<View>();
    view->server = server;
    view->xdg_toplevel = xdg_surface->toplevel;
    view->scene_tree = wlr_scene_xdg_surface_create(server->scene->tree, xdg_surface);
    xdg_surface->data = view->scene_tree;
    
    view->map.notify = xdg_toplevel_map;
    wl_signal_add(&xdg_surface->surface->events.map, &view->map);
    
    view->unmap.notify = xdg_toplevel_unmap;
    wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);
    
    view->destroy.notify = xdg_toplevel_destroy;
    wl_signal_add(&xdg_surface->events.destroy, &view->destroy);
    
    view->request_move.notify = xdg_toplevel_request_move;
    wl_signal_add(&xdg_surface->toplevel->events.request_move, &view->request_move);
    
    view->request_resize.notify = xdg_toplevel_request_resize;
    wl_signal_add(&xdg_surface->toplevel->events.request_resize, &view->request_resize);
    
    view->request_maximize.notify = xdg_toplevel_request_maximize;
    wl_signal_add(&xdg_surface->toplevel->events.request_maximize, &view->request_maximize);
    
    view->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
    wl_signal_add(&xdg_surface->toplevel->events.request_fullscreen, &view->request_fullscreen);
    
    server->views.push_back(std::move(view));
}

// Output rendering
void output_frame(wl_listener* listener, void* data) {
    auto* output = wl_container_of(listener, output, frame);
    auto* server = output->server;
    auto* wlr_output = output->wlr_output;
    
    wlr_scene_output_commit(output->scene_output, nullptr);
    
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(output->scene_output, &now);
    
    // Custom rendering for workspace indicator and command box
    wlr_output_attach_render(wlr_output, nullptr);
    
    wlr_renderer_begin(server->renderer, wlr_output->width, wlr_output->height);
    
    // Render background
    float color[] = {server->bg_color[0], server->bg_color[1], server->bg_color[2], server->bg_color[3]};
    wlr_renderer_clear(server->renderer, color);
    
    // Render scene
    wlr_scene_render_output(server->scene, wlr_output, 0, 0, nullptr);
    
    // Render workspace indicator (top-center)
    const int INDICATOR_HEIGHT = 28;
    const int WS_SPACING = 40;
    int num_ws = static_cast<int>(output->workspaces.size());
    int indicator_width = num_ws * WS_SPACING;
    int indicator_x = (wlr_output->width - indicator_width) / 2;
    int indicator_y = 5;
    
    // Create a simple pixel buffer for text rendering
    std::vector<uint32_t> text_buffer(indicator_width * INDICATOR_HEIGHT, 0x00000000);
    
    for (int i = 0; i < num_ws; ++i) {
        std::string ws_text = std::to_string(i + 1);
        uint32_t text_color = (i == output->current_workspace) ? 0xFFFFFFFF : 0xFFAAAAFF;
        
        // Highlight animation (300ms)
        uint64_t elapsed = 0; // Simplified - would use actual timing
        if (i == output->current_workspace && elapsed < 300000) {
            text_color = 0xFFFFFF00; // Yellow highlight
        }
        
        Font::draw_string(text_buffer.data(), indicator_width, INDICATOR_HEIGHT,
            ws_text, i * WS_SPACING + 10, 8, text_color);
    }
    
    // Command box rendering
    if (server->command_box.active) {
        const int BOX_WIDTH = 600;
        const int BOX_HEIGHT = 40;
        int box_x = (wlr_output->width - BOX_WIDTH) / 2;
        int box_y = wlr_output->height / 2 - BOX_HEIGHT / 2;
        
        std::vector<uint32_t> box_buffer(BOX_WIDTH * BOX_HEIGHT);
        
        // Fill background
        std::fill(box_buffer.begin(), box_buffer.end(), 0xFF222222);
        
        // Draw border
        for (int x = 0; x < BOX_WIDTH; ++x) {
            box_buffer[x] = 0xFFFFFFFF;
            box_buffer[(BOX_HEIGHT - 1) * BOX_WIDTH + x] = 0xFFFFFFFF;
        }
        for (int y = 0; y < BOX_HEIGHT; ++y) {
            box_buffer[y * BOX_WIDTH] = 0xFFFFFFFF;
            box_buffer[y * BOX_WIDTH + BOX_WIDTH - 1] = 0xFFFFFFFF;
        }
        
        // Draw text
        Font::draw_string(box_buffer.data(), BOX_WIDTH, BOX_HEIGHT,
            server->command_box.text, 10, 15, 0xFFFFFFFF);
        
        // Draw cursor
        int cursor_x = 10 + (server->command_box.text.length() * (Font::CHAR_WIDTH + 1));
        for (int y = 15; y < 15 + Font::CHAR_HEIGHT; ++y) {
            if (cursor_x < BOX_WIDTH) {
                box_buffer[y * BOX_WIDTH + cursor_x] = 0xFFFFFFFF;
            }
        }
    }
    
    wlr_renderer_end(server->renderer);
    wlr_output_commit(wlr_output);
}

void output_destroy(wl_listener* listener, void* data) {
    auto* output = wl_container_of(listener, output, destroy);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->destroy.link);
    
    auto& outputs = output->server->outputs;
    outputs.erase(std::remove_if(outputs.begin(), outputs.end(),
        [output](auto& o) { return o.get() == output; }), outputs.end());
}

void server_new_output(wl_listener* listener, void* data) {
    auto* server = wl_container_of(listener, server, new_output);
    auto* wlr_output = static_cast<wlr_output*>(data);
    
    wlr_output_init_render(wlr_output, server->allocator, server->renderer);
    
    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    
    wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output);
    if (mode) {
        wlr_output_state_set_mode(&state, mode);
    }
    
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);
    
    auto output = std::make_unique<Output>();
    output->server = server;
    output->wlr_output = wlr_output;
    output->workspaces.resize(server->config.workspaces);
    
    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    
    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);
    
    wlr_output_layout_add_auto(server->output_layout, wlr_output);
    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
    
    server->outputs.push_back(std::move(output));
}

// Config parsing
std::string get_config_path() {
    const char* xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config) {
        return std::string(xdg_config) + "/eldinwm/eldinwm.conf";
    }
    
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw->pw_dir;
    }
    
    std::string config_home = std::string(home) + "/.config/eldinwm/eldinwm.conf";
    std::ifstream test(config_home);
    if (test.good()) {
        return config_home;
    }
    
    return "/etc/eldinwm/eldinwm.conf";
}

Config parse_config(const std::string& path) {
    Config config;
    std::ifstream file(path);
    
    if (!file.is_open()) {
        fprintf(stderr, "ElDinWM: Could not open config file: %s\n", path.c_str());
        fprintf(stderr, "ElDinWM: Using defaults (4 workspaces, no background image)\n");
        return config;
    }
    
    std::string line;
    bool found_workspaces = false;
    
    while (std::getline(file, line)) {
        // Remove comments
        size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
        }
        
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\n\r"));
        line.erase(line.find_last_not_of(" \t\n\r") + 1);
        
        if (line.empty()) continue;
        
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        
        // Trim key and value
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t\""));
        value.erase(value.find_last_not_of(" \t\"") + 1);
        
        if (key == "workspaces") {
            try {
                config.workspaces = std::stoi(value);
                if (config.workspaces < 1) {
                    fprintf(stderr, "ElDinWM: Invalid workspaces value, using 4\n");
                    config.workspaces = 4;
                }
                found_workspaces = true;
            } catch (...) {
                fprintf(stderr, "ElDinWM: Invalid workspaces value, using 4\n");
                config.workspaces = 4;
            }
        } else if (key == "background_image") {
            config.background_image = value;
        }
    }
    
    if (!found_workspaces) {
        fprintf(stderr, "ElDinWM: 'workspaces' key required in config, using default 4\n");
    }
    
    return config;
}

// Signal handlers
Server* g_server = nullptr;

void handle_signal(int sig) {
    if (g_server) {
        wl_display_terminate(g_server->wl_display);
        g_server->running = false;
    }
}

// Main
int main(int argc, char* argv[]) {
    wlr_log_init(WLR_ERROR, nullptr);
    
    Server server;
    g_server = &server;
    
    // Setup signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // Parse config
    std::string config_path = get_config_path();
    server.config = parse_config(config_path);
    
    fprintf(stderr, "ElDinWM: Starting with %d workspaces\n", server.config.workspaces);
    
    // Initialize Wayland display
    server.wl_display = wl_display_create();
    if (!server.wl_display) {
        fprintf(stderr, "ElDinWM: Failed to create Wayland display\n");
        return 1;
    }
    
    // Backend
    server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.wl_display), nullptr);
    if (!server.backend) {
        fprintf(stderr, "ElDinWM: Failed to create backend\n");
        return 1;
    }
    
    // Renderer
    server.renderer = wlr_renderer_autocreate(server.backend);
    if (!server.renderer) {
        fprintf(stderr, "ElDinWM: Failed to create renderer\n");
        return 1;
    }
    
    wlr_renderer_init_wl_display(server.renderer, server.wl_display);
    
    // Allocator
    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
    if (!server.allocator) {
        fprintf(stderr, "ElDinWM: Failed to create allocator\n");
        return 1;
    }
    
    // Compositor
    wlr_compositor_create(server.wl_display, 5, server.renderer);
    wlr_subcompositor_create(server.wl_display);
    wlr_data_device_manager_create(server.wl_display);
    
    // Output layout
    server.output_layout = wlr_output_layout_create(server.wl_display);
    
    // Scene
    server.scene = wlr_scene_create();
    server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);
    
    // XDG shell
    server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
    server.new_xdg_surface.notify = server_new_xdg_surface;
    wl_signal_add(&server.xdg_shell->events.new_surface, &server.new_xdg_surface);
    
    // Cursor
    server.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
    
    server.cursor_mgr = wlr_xcursor_manager_create(nullptr, 24);
    
    server.cursor_motion.notify = server_cursor_motion;
    wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
    
    server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);
    
    server.cursor_button.notify = server_cursor_button;
    wl_signal_add(&server.cursor->events.button, &server.cursor_button);
    
    server.cursor_axis.notify = server_cursor_axis;
    wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
    
    server.cursor_frame.notify = server_cursor_frame;
    wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);
    
    // Seat
    server.seat = wlr_seat_create(server.wl_display, "seat0");
    server.request_cursor.notify = seat_request_cursor;
    wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);
    
    server.request_set_selection.notify = seat_request_set_selection;
    wl_signal_add(&server.seat->events.request_set_selection, &server.request_set_selection);
    
    // Input
    server.new_input.notify = server_new_input;
    wl_signal_add(&server.backend->events.new_input, &server.new_input);
    
    // Output
    server.new_output.notify = server_new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);
    
    // Start backend
    const char* socket = wl_display_add_socket_auto(server.wl_display);
    if (!socket) {
        fprintf(stderr, "ElDinWM: Failed to create socket\n");
        wlr_backend_destroy(server.backend);
        return 1;
    }
    
    if (!wlr_backend_start(server.backend)) {
        fprintf(stderr, "ElDinWM: Failed to start backend\n");
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }
    
    setenv("WAYLAND_DISPLAY", socket, 1);
    fprintf(stderr, "ElDinWM: Running on WAYLAND_DISPLAY=%s\n", socket);
    
    // Run
    wl_display_run(server.wl_display);
    
    // Cleanup
    wl_display_destroy_clients(server.wl_display);
    wlr_scene_node_destroy(&server.scene->tree.node);
    wlr_xcursor_manager_destroy(server.cursor_mgr);
    wlr_cursor_destroy(server.cursor);
    wlr_allocator_destroy(server.allocator);
    wlr_renderer_destroy(server.renderer);
    wlr_backend_destroy(server.backend);
    wl_display_destroy(server.wl_display);
    
    fprintf(stderr, "ElDinWM: Exiting cleanly\n");
    return 0;
}
