/*
 * ElDinWM - Minimalist Wayland Tiling Compositor
 * Written in pure C with wlroots 0.18
 * License: MIT
 */

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pwd.h>

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

#define MAX_VIEWS 32
#define MAX_OUTPUTS 8
#define MAX_KEYBOARDS 8
#define MAX_WORKSPACES 16
#define VIEWS_PER_WORKSPACE 2
#define MAX_COMMAND_LENGTH 512
#define INDICATOR_HEIGHT 28

/* Embedded bitmap font (7x9 pixels per char, ASCII 32-126) */
#define FONT_CHAR_WIDTH 7
#define FONT_CHAR_HEIGHT 9
#define FONT_FIRST_CHAR 32
#define FONT_LAST_CHAR 126

static const uint8_t font_data[][9] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* Space */
    {0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x08, 0x00, 0x00}, /* ! */
    {0x14, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* " */
    {0x14, 0x14, 0x3E, 0x14, 0x3E, 0x14, 0x14, 0x00, 0x00}, /* # */
    {0x08, 0x1E, 0x28, 0x1C, 0x0A, 0x3C, 0x08, 0x00, 0x00}, /* $ */
    {0x30, 0x32, 0x04, 0x08, 0x10, 0x26, 0x06, 0x00, 0x00}, /* % */
    {0x10, 0x28, 0x28, 0x10, 0x2A, 0x24, 0x1A, 0x00, 0x00}, /* & */
    {0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ' */
    {0x04, 0x08, 0x10, 0x10, 0x10, 0x08, 0x04, 0x00, 0x00}, /* ( */
    {0x10, 0x08, 0x04, 0x04, 0x04, 0x08, 0x10, 0x00, 0x00}, /* ) */
    {0x00, 0x08, 0x2A, 0x1C, 0x2A, 0x08, 0x00, 0x00, 0x00}, /* * */
    {0x00, 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, 0x00, 0x00}, /* + */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x10, 0x00}, /* , */
    {0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00}, /* - */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00}, /* . */
    {0x00, 0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x00, 0x00}, /* / */
    {0x1C, 0x22, 0x26, 0x2A, 0x32, 0x22, 0x1C, 0x00, 0x00}, /* 0 */
    {0x08, 0x18, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00}, /* 1 */
    {0x1C, 0x22, 0x02, 0x0C, 0x10, 0x20, 0x3E, 0x00, 0x00}, /* 2 */
    {0x1C, 0x22, 0x02, 0x0C, 0x02, 0x22, 0x1C, 0x00, 0x00}, /* 3 */
    {0x04, 0x0C, 0x14, 0x24, 0x3E, 0x04, 0x04, 0x00, 0x00}, /* 4 */
    {0x3E, 0x20, 0x3C, 0x02, 0x02, 0x22, 0x1C, 0x00, 0x00}, /* 5 */
    {0x0C, 0x10, 0x20, 0x3C, 0x22, 0x22, 0x1C, 0x00, 0x00}, /* 6 */
    {0x3E, 0x02, 0x04, 0x08, 0x10, 0x10, 0x10, 0x00, 0x00}, /* 7 */
    {0x1C, 0x22, 0x22, 0x1C, 0x22, 0x22, 0x1C, 0x00, 0x00}, /* 8 */
    {0x1C, 0x22, 0x22, 0x1E, 0x02, 0x04, 0x18, 0x00, 0x00}, /* 9 */
    {0x00, 0x00, 0x08, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00}, /* : */
    {0x00, 0x00, 0x08, 0x00, 0x00, 0x08, 0x08, 0x10, 0x00}, /* ; */
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02, 0x00, 0x00}, /* < */
    {0x00, 0x00, 0x3E, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00}, /* = */
    {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00}, /* > */
    {0x1C, 0x22, 0x04, 0x08, 0x08, 0x00, 0x08, 0x00, 0x00}, /* ? */
    {0x1C, 0x22, 0x2E, 0x2A, 0x2E, 0x20, 0x1C, 0x00, 0x00}, /* @ */
    {0x08, 0x14, 0x22, 0x22, 0x3E, 0x22, 0x22, 0x00, 0x00}, /* A */
    {0x3C, 0x22, 0x22, 0x3C, 0x22, 0x22, 0x3C, 0x00, 0x00}, /* B */
    {0x1C, 0x22, 0x20, 0x20, 0x20, 0x22, 0x1C, 0x00, 0x00}, /* C */
    {0x38, 0x24, 0x22, 0x22, 0x22, 0x24, 0x38, 0x00, 0x00}, /* D */
    {0x3E, 0x20, 0x20, 0x3C, 0x20, 0x20, 0x3E, 0x00, 0x00}, /* E */
    {0x3E, 0x20, 0x20, 0x3C, 0x20, 0x20, 0x20, 0x00, 0x00}, /* F */
    {0x1C, 0x22, 0x20, 0x2E, 0x22, 0x22, 0x1C, 0x00, 0x00}, /* G */
    {0x22, 0x22, 0x22, 0x3E, 0x22, 0x22, 0x22, 0x00, 0x00}, /* H */
    {0x1C, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00}, /* I */
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x24, 0x18, 0x00, 0x00}, /* J */
    {0x22, 0x24, 0x28, 0x30, 0x28, 0x24, 0x22, 0x00, 0x00}, /* K */
    {0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x3E, 0x00, 0x00}, /* L */
    {0x22, 0x36, 0x2A, 0x2A, 0x22, 0x22, 0x22, 0x00, 0x00}, /* M */
    {0x22, 0x32, 0x2A, 0x26, 0x22, 0x22, 0x22, 0x00, 0x00}, /* N */
    {0x1C, 0x22, 0x22, 0x22, 0x22, 0x22, 0x1C, 0x00, 0x00}, /* O */
    {0x3C, 0x22, 0x22, 0x3C, 0x20, 0x20, 0x20, 0x00, 0x00}, /* P */
    {0x1C, 0x22, 0x22, 0x22, 0x2A, 0x24, 0x1A, 0x00, 0x00}, /* Q */
    {0x3C, 0x22, 0x22, 0x3C, 0x28, 0x24, 0x22, 0x00, 0x00}, /* R */
    {0x1C, 0x22, 0x20, 0x1C, 0x02, 0x22, 0x1C, 0x00, 0x00}, /* S */
    {0x3E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00}, /* T */
    {0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x1C, 0x00, 0x00}, /* U */
    {0x22, 0x22, 0x22, 0x22, 0x14, 0x14, 0x08, 0x00, 0x00}, /* V */
    {0x22, 0x22, 0x22, 0x2A, 0x2A, 0x36, 0x22, 0x00, 0x00}, /* W */
    {0x22, 0x22, 0x14, 0x08, 0x14, 0x22, 0x22, 0x00, 0x00}, /* X */
    {0x22, 0x22, 0x14, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00}, /* Y */
    {0x3E, 0x02, 0x04, 0x08, 0x10, 0x20, 0x3E, 0x00, 0x00}, /* Z */
    {0x1C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1C, 0x00, 0x00}, /* [ */
    {0x00, 0x20, 0x10, 0x08, 0x04, 0x02, 0x00, 0x00, 0x00}, /* \ */
    {0x1C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1C, 0x00, 0x00}, /* ] */
    {0x08, 0x14, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ^ */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x00}, /* _ */
    {0x10, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ` */
    {0x00, 0x00, 0x1C, 0x02, 0x1E, 0x22, 0x1E, 0x00, 0x00}, /* a */
    {0x20, 0x20, 0x3C, 0x22, 0x22, 0x22, 0x3C, 0x00, 0x00}, /* b */
    {0x00, 0x00, 0x1C, 0x22, 0x20, 0x22, 0x1C, 0x00, 0x00}, /* c */
    {0x02, 0x02, 0x1E, 0x22, 0x22, 0x22, 0x1E, 0x00, 0x00}, /* d */
    {0x00, 0x00, 0x1C, 0x22, 0x3E, 0x20, 0x1C, 0x00, 0x00}, /* e */
    {0x0C, 0x12, 0x10, 0x3C, 0x10, 0x10, 0x10, 0x00, 0x00}, /* f */
    {0x00, 0x00, 0x1E, 0x22, 0x22, 0x1E, 0x02, 0x1C, 0x00}, /* g */
    {0x20, 0x20, 0x3C, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00}, /* h */
    {0x08, 0x00, 0x18, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00}, /* i */
    {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x24, 0x18, 0x00}, /* j */
    {0x20, 0x20, 0x22, 0x24, 0x38, 0x24, 0x22, 0x00, 0x00}, /* k */
    {0x18, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00}, /* l */
    {0x00, 0x00, 0x36, 0x2A, 0x2A, 0x2A, 0x22, 0x00, 0x00}, /* m */
    {0x00, 0x00, 0x3C, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00}, /* n */
    {0x00, 0x00, 0x1C, 0x22, 0x22, 0x22, 0x1C, 0x00, 0x00}, /* o */
    {0x00, 0x00, 0x3C, 0x22, 0x22, 0x3C, 0x20, 0x20, 0x00}, /* p */
    {0x00, 0x00, 0x1E, 0x22, 0x22, 0x1E, 0x02, 0x02, 0x00}, /* q */
    {0x00, 0x00, 0x2C, 0x32, 0x20, 0x20, 0x20, 0x00, 0x00}, /* r */
    {0x00, 0x00, 0x1E, 0x20, 0x1C, 0x02, 0x3C, 0x00, 0x00}, /* s */
    {0x10, 0x10, 0x3C, 0x10, 0x10, 0x12, 0x0C, 0x00, 0x00}, /* t */
    {0x00, 0x00, 0x22, 0x22, 0x22, 0x26, 0x1A, 0x00, 0x00}, /* u */
    {0x00, 0x00, 0x22, 0x22, 0x22, 0x14, 0x08, 0x00, 0x00}, /* v */
    {0x00, 0x00, 0x22, 0x22, 0x2A, 0x2A, 0x14, 0x00, 0x00}, /* w */
    {0x00, 0x00, 0x22, 0x14, 0x08, 0x14, 0x22, 0x00, 0x00}, /* x */
    {0x00, 0x00, 0x22, 0x22, 0x22, 0x1E, 0x02, 0x1C, 0x00}, /* y */
    {0x00, 0x00, 0x3E, 0x04, 0x08, 0x10, 0x3E, 0x00, 0x00}, /* z */
    {0x0E, 0x08, 0x08, 0x30, 0x08, 0x08, 0x0E, 0x00, 0x00}, /* { */
    {0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00}, /* | */
    {0x38, 0x08, 0x08, 0x06, 0x08, 0x08, 0x38, 0x00, 0x00}, /* } */
    {0x00, 0x00, 0x00, 0x32, 0x4C, 0x00, 0x00, 0x00, 0x00}, /* ~ */
};

/* Forward declarations */
struct server;
struct output;
struct view;
struct workspace;

/* Configuration */
struct config {
    int num_workspaces;
    char background_image[512];
};

/* Workspace holds up to 2 views */
struct workspace {
    struct view *views[VIEWS_PER_WORKSPACE];
    int view_count;
};

/* View represents a window */
struct view {
    struct server *server;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree *scene_tree;
    
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    
    int workspace_idx;
    bool mapped;
};

/* Output represents a monitor */
struct output {
    struct server *server;
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;
    
    struct wl_listener frame;
    struct wl_listener destroy_listener;
    
    int current_workspace;
    struct workspace workspaces[MAX_WORKSPACES];
    struct timespec workspace_switch_time;
};

/* Command box state */
struct command_box {
    bool active;
    char text[MAX_COMMAND_LENGTH];
    int length;
};

/* Server global state */
struct server {
    struct wl_display *wl_display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;
    
    struct wlr_xdg_shell *xdg_shell;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wlr_seat *seat;
    struct wlr_output_layout *output_layout;
    
    struct view *views[MAX_VIEWS];
    int view_count;
    
    struct output *outputs[MAX_OUTPUTS];
    int output_count;
    
    struct wlr_keyboard *keyboards[MAX_KEYBOARDS];
    int keyboard_count;
    
    struct wl_listener new_output;
    struct wl_listener new_xdg_surface;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    struct wl_listener new_input;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;
    
    struct config config;
    struct command_box command_box;
    bool running;
    
    float bg_color[4];
};

/* Global server pointer for signal handling */
static struct server *g_server = NULL;

/* Font rendering */
static void draw_char(uint32_t *buffer, int buf_width, int buf_height,
                     char c, int x, int y, uint32_t color) {
    if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR) return;
    
    const uint8_t *glyph = font_data[c - FONT_FIRST_CHAR];
    
    for (int row = 0; row < FONT_CHAR_HEIGHT; row++) {
        for (int col = 0; col < FONT_CHAR_WIDTH; col++) {
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

static void draw_string(uint32_t *buffer, int buf_width, int buf_height,
                       const char *text, int x, int y, uint32_t color) {
    int cursor_x = x;
    for (const char *p = text; *p; p++) {
        draw_char(buffer, buf_width, buf_height, *p, cursor_x, y, color);
        cursor_x += FONT_CHAR_WIDTH + 1;
    }
}

/* Layout computation */
static void relayout_workspace(struct output *output);

/* Add view to workspace */
static void add_view_to_workspace(struct output *output, int ws_idx, struct view *view) {
    struct workspace *ws = &output->workspaces[ws_idx];
    if (ws->view_count < VIEWS_PER_WORKSPACE) {
        ws->views[ws->view_count++] = view;
        view->workspace_idx = ws_idx;
    }
}

/* Remove view from workspace */
static void remove_view_from_workspace(struct view *view) {
    struct server *server = view->server;
    
    for (int i = 0; i < server->output_count; i++) {
        struct output *output = server->outputs[i];
        for (int j = 0; j < server->config.num_workspaces; j++) {
            struct workspace *ws = &output->workspaces[j];
            for (int k = 0; k < ws->view_count; k++) {
                if (ws->views[k] == view) {
                    /* Shift remaining views */
                    for (int l = k; l < ws->view_count - 1; l++) {
                        ws->views[l] = ws->views[l + 1];
                    }
                    ws->view_count--;
                    relayout_workspace(output);
                    return;
                }
            }
        }
    }
}

/* Find available workspace slot */
static bool find_available_workspace(struct server *server, struct output **out_output, int *out_ws) {
    for (int i = 0; i < server->output_count; i++) {
        struct output *output = server->outputs[i];
        for (int j = 0; j < server->config.num_workspaces; j++) {
            if (output->workspaces[j].view_count < VIEWS_PER_WORKSPACE) {
                *out_output = output;
                *out_ws = j;
                return true;
            }
        }
    }
    return false;
}

/* Layout windows in workspace */
static void relayout_workspace(struct output *output) {
    if (!output || !output->wlr_output) return;
    
    struct workspace *ws = &output->workspaces[output->current_workspace];
    int width = output->wlr_output->width;
    int height = output->wlr_output->height;
    int usable_height = height - INDICATOR_HEIGHT;
    
    /* Hide all views first */
    for (int i = 0; i < output->server->view_count; i++) {
        struct view *v = output->server->views[i];
        if (v && v->mapped) {
            wlr_scene_node_set_enabled(&v->scene_tree->node, false);
        }
    }
    
    /* Show and position views in current workspace */
    if (ws->view_count == 1) {
        /* Fullscreen */
        struct view *v = ws->views[0];
        if (v && v->mapped) {
            wlr_scene_node_set_enabled(&v->scene_tree->node, true);
            wlr_scene_node_set_position(&v->scene_tree->node, 0, INDICATOR_HEIGHT);
            wlr_xdg_toplevel_set_size(v->xdg_toplevel, width, usable_height);
        }
    } else if (ws->view_count == 2) {
        /* 50/50 split */
        int half_width = width / 2;
        for (int i = 0; i < 2; i++) {
            struct view *v = ws->views[i];
            if (v && v->mapped) {
                wlr_scene_node_set_enabled(&v->scene_tree->node, true);
                wlr_scene_node_set_position(&v->scene_tree->node,
                    i == 0 ? 0 : half_width, INDICATOR_HEIGHT);
                wlr_xdg_toplevel_set_size(v->xdg_toplevel, half_width, usable_height);
            }
        }
    }
}

/* Execute shell command */
static void execute_command(const char *cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "/bin/sh", "-lc", cmd, NULL);
        _exit(1);
    } else if (pid > 0) {
        signal(SIGCHLD, SIG_IGN);
    }
}

/* Keyboard handling */
static void handle_keybinding(struct server *server, xkb_keysym_t sym, uint32_t modifiers) {
    const uint32_t CTRL_SHIFT = WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT;
    
    if (server->command_box.active) {
        if (sym == XKB_KEY_Escape) {
            server->command_box.active = false;
            server->command_box.length = 0;
            server->command_box.text[0] = '\0';
        } else if (sym == XKB_KEY_Return) {
            if (server->command_box.length > 0) {
                execute_command(server->command_box.text);
            }
            server->command_box.active = false;
            server->command_box.length = 0;
            server->command_box.text[0] = '\0';
        } else if (sym == XKB_KEY_BackSpace) {
            if (server->command_box.length > 0) {
                server->command_box.length--;
                server->command_box.text[server->command_box.length] = '\0';
            }
        } else if (sym >= 32 && sym < 127) {
            if (server->command_box.length < MAX_COMMAND_LENGTH - 1) {
                server->command_box.text[server->command_box.length++] = (char)sym;
                server->command_box.text[server->command_box.length] = '\0';
            }
        }
        
        /* Force redraw */
        for (int i = 0; i < server->output_count; i++) {
            wlr_output_schedule_frame(server->outputs[i]->wlr_output);
        }
        return;
    }
    
    /* Global shortcuts */
    if (modifiers == CTRL_SHIFT) {
        switch (sym) {
            case XKB_KEY_Down:
                wl_display_terminate(server->wl_display);
                server->running = false;
                break;
                
            case XKB_KEY_Left:
            case XKB_KEY_Right: {
                int delta = (sym == XKB_KEY_Right) ? 1 : -1;
                for (int i = 0; i < server->output_count; i++) {
                    struct output *output = server->outputs[i];
                    int new_ws = output->current_workspace + delta;
                    if (new_ws >= 0 && new_ws < server->config.num_workspaces) {
                        output->current_workspace = new_ws;
                        clock_gettime(CLOCK_MONOTONIC, &output->workspace_switch_time);
                        relayout_workspace(output);
                        wlr_output_schedule_frame(output->wlr_output);
                    }
                }
                break;
            }
            
            case XKB_KEY_z:
            case XKB_KEY_Z:
                server->command_box.active = true;
                server->command_box.length = 0;
                server->command_box.text[0] = '\0';
                for (int i = 0; i < server->output_count; i++) {
                    wlr_output_schedule_frame(server->outputs[i]->wlr_output);
                }
                break;
                
            case XKB_KEY_x:
            case XKB_KEY_X:
                for (int i = 0; i < server->output_count; i++) {
                    struct output *output = server->outputs[i];
                    struct workspace *ws = &output->workspaces[output->current_workspace];
                    if (ws->view_count == 2) {
                        struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
                        struct view *other = NULL;
                        for (int j = 0; j < 2; j++) {
                            if (ws->views[j]->xdg_toplevel->base->surface != focused) {
                                other = ws->views[j];
                                break;
                            }
                        }
                        if (other && other->mapped) {
                            wlr_seat_keyboard_notify_enter(server->seat,
                                other->xdg_toplevel->base->surface, NULL, 0, NULL);
                        }
                    }
                }
                break;
        }
    }
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
    struct wlr_keyboard *kb = wl_container_of(listener, kb, events.modifiers);
    struct server *server = kb->data;
    wlr_seat_set_keyboard(server->seat, kb);
    wlr_seat_keyboard_notify_modifiers(server->seat, &kb->modifiers);
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
    struct wlr_keyboard_key_event *event = data;
    struct wlr_keyboard *kb = wl_container_of(listener, kb, events.key);
    struct server *server = kb->data;
    
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        uint32_t keycode = event->keycode + 8;
        const xkb_keysym_t *syms;
        int nsyms = xkb_state_key_get_syms(kb->xkb_state, keycode, &syms);
        uint32_t modifiers = wlr_keyboard_get_modifiers(kb);
        
        for (int i = 0; i < nsyms; i++) {
            handle_keybinding(server, syms[i], modifiers);
        }
    }
    
    wlr_seat_set_keyboard(server->seat, kb);
    wlr_seat_keyboard_notify_key(server->seat, event->time_msec,
        event->keycode, event->state);
}

static void server_new_keyboard(struct server *server, struct wlr_input_device *device) {
    struct wlr_keyboard *kb = wlr_keyboard_from_input_device(device);
    kb->data = server;
    
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    
    wlr_keyboard_set_keymap(kb, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(kb, 25, 600);
    
    /* Note: These listeners are leaked on purpose for simplicity */
    struct wl_listener *mod_listener = calloc(1, sizeof(struct wl_listener));
    struct wl_listener *key_listener = calloc(1, sizeof(struct wl_listener));
    
    mod_listener->notify = keyboard_handle_modifiers;
    key_listener->notify = keyboard_handle_key;
    
    wl_signal_add(&kb->events.modifiers, mod_listener);
    wl_signal_add(&kb->events.key, key_listener);
    
    wlr_seat_set_keyboard(server->seat, kb);
    
    if (server->keyboard_count < MAX_KEYBOARDS) {
        server->keyboards[server->keyboard_count++] = kb;
    }
}

static void server_new_pointer(struct server *server, struct wlr_input_device *device) {
    wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_input(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;
    
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

/* Cursor handling */
static void process_cursor_motion(struct server *server, uint32_t time) {
    wlr_seat_pointer_notify_motion(server->seat, time,
        server->cursor->x, server->cursor->y);
}

static void server_cursor_motion(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    process_cursor_motion(server, event->time_msec);
}

static void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(server, event->time_msec);
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;
    wlr_seat_pointer_notify_button(server->seat, event->time_msec,
        event->button, event->state);
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
        event->orientation, event->delta, event->delta_discrete, event->source,
        event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}

static void seat_request_cursor(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;
    
    if (focused_client == event->seat_client) {
        wlr_cursor_set_surface(server->cursor, event->surface,
            event->hotspot_x, event->hotspot_y);
    }
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

/* View event handlers */
static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    struct view *view = wl_container_of(listener, view, map);
    view->mapped = true;
    
    struct output *output = NULL;
    int ws_idx = 0;
    if (find_available_workspace(view->server, &output, &ws_idx)) {
        add_view_to_workspace(output, ws_idx, view);
        relayout_workspace(output);
        wlr_seat_keyboard_notify_enter(view->server->seat,
            view->xdg_toplevel->base->surface, NULL, 0, NULL);
    } else {
        wlr_scene_node_set_enabled(&view->scene_tree->node, false);
        fprintf(stderr, "ElDinWM: Cannot map window - all workspaces full\n");
    }
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
    struct view *view = wl_container_of(listener, view, unmap);
    view->mapped = false;
    remove_view_from_workspace(view);
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
    struct view *view = wl_container_of(listener, view, destroy);
    
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->request_move.link);
    wl_list_remove(&view->request_resize.link);
    wl_list_remove(&view->request_maximize.link);
    wl_list_remove(&view->request_fullscreen.link);
    
    /* Remove from server views array */
    for (int i = 0; i < view->server->view_count; i++) {
        if (view->server->views[i] == view) {
            for (int j = i; j < view->server->view_count - 1; j++) {
                view->server->views[j] = view->server->views[j + 1];
            }
            view->server->view_count--;
            break;
        }
    }
    
    free(view);
}

static void xdg_toplevel_request_move(struct wl_listener *listener, void *data) {
    /* No drag-to-move support */
}

static void xdg_toplevel_request_resize(struct wl_listener *listener, void *data) {
    /* No manual resize support */
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data) {
    struct view *view = wl_container_of(listener, view, request_maximize);
    wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data) {
    struct view *view = wl_container_of(listener, view, request_fullscreen);
    wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, false);
}

static void server_new_xdg_surface(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, new_xdg_surface);
    struct wlr_xdg_surface *xdg_surface = data;
    
    if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        return;
    }
    
    if (server->view_count >= MAX_VIEWS) {
        fprintf(stderr, "ElDinWM: Maximum views reached\n");
        return;
    }
    
    struct view *view = calloc(1, sizeof(struct view));
    view->server = server;
    view->xdg_toplevel = xdg_surface->toplevel;
    view->scene_tree = wlr_scene_xdg_surface_create(&server->scene->tree, xdg_surface);
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
    
    server->views[server->view_count++] = view;
}

/* Output rendering */
static void output_frame(struct wl_listener *listener, void *data) {
    struct output *output = wl_container_of(listener, output, frame);
    struct server *server = output->server;
    struct wlr_output *wlr_output = output->wlr_output;
    
    struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
        server->scene, wlr_output);
    wlr_scene_output_commit(scene_output, NULL);
    
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_destroy(struct wl_listener *listener, void *data) {
    struct output *output = wl_container_of(listener, output, destroy_listener);
    
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->destroy_listener.link);
    
    /* Remove from server outputs array */
    for (int i = 0; i < output->server->output_count; i++) {
        if (output->server->outputs[i] == output) {
            for (int j = i; j < output->server->output_count - 1; j++) {
                output->server->outputs[j] = output->server->outputs[j + 1];
            }
            output->server->output_count--;
            break;
        }
    }
    
    free(output);
}

static void server_new_output(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;
    
    wlr_output_init_render(wlr_output, server->allocator, server->renderer);
    
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode) {
        wlr_output_state_set_mode(&state, mode);
    }
    
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);
    
    if (server->output_count >= MAX_OUTPUTS) {
        fprintf(stderr, "ElDinWM: Maximum outputs reached\n");
        return;
    }
    
    struct output *output = calloc(1, sizeof(struct output));
    output->server = server;
    output->wlr_output = wlr_output;
    
    /* Initialize workspaces */
    for (int i = 0; i < server->config.num_workspaces; i++) {
        output->workspaces[i].view_count = 0;
    }
    
    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    
    output->destroy_listener.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy_listener);
    
    wlr_output_layout_add_auto(server->output_layout, wlr_output);
    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
    
    server->outputs[server->output_count++] = output;
}

/* Config parsing */
static void get_config_path(char *path, size_t size) {
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config) {
        snprintf(path, size, "%s/eldinwm/eldinwm.conf", xdg_config);
        if (access(path, F_OK) == 0) return;
    }
    
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw->pw_dir;
    }
    
    snprintf(path, size, "%s/.config/eldinwm/eldinwm.conf", home);
    if (access(path, F_OK) == 0) return;
    
    snprintf(path, size, "/etc/eldinwm/eldinwm.conf");
}

static void trim(char *str) {
    char *start = str;
    while (isspace(*start)) start++;
    
    char *end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) end--;
    *(end + 1) = '\0';
    
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

static void parse_config(struct config *config, const char *path) {
    /* Defaults */
    config->num_workspaces = 4;
    config->background_image[0] = '\0';
    
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "ElDinWM: Could not open config: %s\n", path);
        fprintf(stderr, "ElDinWM: Using defaults (4 workspaces)\n");
        return;
    }
    
    char line[1024];
    bool found_workspaces = false;
    
    while (fgets(line, sizeof(line), file)) {
        /* Remove comments */
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        
        trim(line);
        if (line[0] == '\0') continue;
        
        char *eq = strchr(line, '=');
        if (!eq) continue;
        
        *eq = '\0';
        char *key = line;
        char *value = eq + 1;
        
        trim(key);
        trim(value);
        
        /* Remove quotes from value */
        if (value[0] == '"') {
            value++;
            char *end_quote = strchr(value, '"');
            if (end_quote) *end_quote = '\0';
        }
        
        if (strcmp(key, "workspaces") == 0) {
            int ws = atoi(value);
            if (ws >= 1 && ws <= MAX_WORKSPACES) {
                config->num_workspaces = ws;
                found_workspaces = true;
            }
        } else if (strcmp(key, "background_image") == 0) {
            strncpy(config->background_image, value, sizeof(config->background_image) - 1);
        }
    }
    
    fclose(file);
    
    if (!found_workspaces) {
        fprintf(stderr, "ElDinWM: 'workspaces' key required in config\n");
    }
}

/* Signal handler */
static void handle_signal(int sig) {
    if (g_server) {
        wl_display_terminate(g_server->wl_display);
        g_server->running = false;
    }
}

/* Main */
int main(int argc, char *argv[]) {
    wlr_log_init(WLR_ERROR, NULL);
    
    struct server server = {0};
    g_server = &server;
    
    /* Setup signal handlers */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGCHLD, SIG_IGN);
    
    /* Parse config */
    char config_path[512];
    get_config_path(config_path, sizeof(config_path));
    parse_config(&server.config, config_path);
    
    fprintf(stderr, "ElDinWM: Starting with %d workspaces\n", server.config.num_workspaces);
    
    /* Background color (dark blue) */
    server.bg_color[0] = 0.0f;
    server.bg_color[1] = 0.05f;
    server.bg_color[2] = 0.15f;
    server.bg_color[3] = 1.0f;
    
    /* Initialize Wayland display */
    server.wl_display = wl_display_create();
    if (!server.wl_display) {
        fprintf(stderr, "ElDinWM: Failed to create Wayland display\n");
        return 1;
    }
    
    /* Backend */
    server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.wl_display), NULL);
    if (!server.backend) {
        fprintf(stderr, "ElDinWM: Failed to create backend\n");
        return 1;
    }
    
    /* Renderer */
    server.renderer = wlr_renderer_autocreate(server.backend);
    if (!server.renderer) {
        fprintf(stderr, "ElDinWM: Failed to create renderer\n");
        return 1;
    }
    
    wlr_renderer_init_wl_display(server.renderer, server.wl_display);
    
    /* Allocator */
    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
    if (!server.allocator) {
        fprintf(stderr, "ElDinWM: Failed to create allocator\n");
        return 1;
    }
    
    /* Compositor */
    wlr_compositor_create(server.wl_display, 5, server.renderer);
    wlr_subcompositor_create(server.wl_display);
    wlr_data_device_manager_create(server.wl_display);
    
    /* Output layout */
    server.output_layout = wlr_output_layout_create(server.wl_display);
    
    /* Scene */
    server.scene = wlr_scene_create();
    server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);
    
    /* XDG shell */
    server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
    server.new_xdg_surface.notify = server_new_xdg_surface;
    wl_signal_add(&server.xdg_shell->events.new_surface, &server.new_xdg_surface);
    
    /* Cursor */
    server.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
    
    server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    
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
    
    /* Seat */
    server.seat = wlr_seat_create(server.wl_display, "seat0");
    server.request_cursor.notify = seat_request_cursor;
    wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);
    
    server.request_set_selection.notify = seat_request_set_selection;
    wl_signal_add(&server.seat->events.request_set_selection, &server.request_set_selection);
    
    /* Input */
    server.new_input.notify = server_new_input;
    wl_signal_add(&server.backend->events.new_input, &server.new_input);
    
    /* Output */
    server.new_output.notify = server_new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);
    
    /* Start backend */
    const char *socket = wl_display_add_socket_auto(server.wl_display);
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
    
    server.running = true;
    
    /* Run */
    wl_display_run(server.wl_display);
    
    /* Cleanup */
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
