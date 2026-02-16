/*
 * ElDinWM - Complete Implementation
 * All features: tiling, workspaces, command box, focus cycling, config
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
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
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#define MAX_WORKSPACES 16
#define VIEWS_PER_WS 2
#define MAX_CMD_LEN 512

/* Configuration */
struct config {
    int num_workspaces;
    char background_image[512];
};

/* Forward declarations */
struct server;
struct output;
struct view;

/* Command box state */
struct cmdbox {
    bool active;
    char text[MAX_CMD_LEN];
    int len;
};

/* Server */
struct server {
    struct wl_display *display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;
    struct wlr_seat *seat;
    struct wlr_output_layout *output_layout;
    struct wlr_xdg_shell *xdg_shell;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    
    struct wl_listener new_output;
    struct wl_listener new_xdg_surface;
    struct wl_listener new_input;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;
    
    struct wl_list outputs;
    struct wl_list views;
    struct wl_list keyboards;
    
    struct config config;
    struct cmdbox cmdbox;
    bool running;
};

/* Output */
struct output {
    struct wl_list link;
    struct server *server;
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;
    struct wl_listener frame;
    struct wl_listener destroy;
    
    int current_ws;
    struct wl_list workspaces[MAX_WORKSPACES];
    struct timespec ws_switch_time;
};

/* View */
struct view {
    struct wl_list link;
    struct wl_list ws_link;
    struct server *server;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree *scene_tree;
    
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener request_fullscreen;
    
    struct output *output;
    int workspace;
    bool mapped;
};

/* Keyboard */
struct keyboard {
    struct wl_list link;
    struct server *server;
    struct wlr_keyboard *wlr_keyboard;
    struct wl_listener modifiers;
    struct wl_listener key;
};

static struct server g_server = {0};

/* Utility functions */
static int count_workspace_views(struct output *output, int ws) {
    int count = 0;
    struct view *v;
    wl_list_for_each(v, &output->workspaces[ws], ws_link) {
        count++;
    }
    return count;
}

static void layout_workspace(struct output *output) {
    if (!output->wlr_output) return;
    
    int width = output->wlr_output->width;
    int height = output->wlr_output->height;
    
    /* Hide all views */
    struct view *view;
    wl_list_for_each(view, &output->server->views, link) {
        if (view->mapped) {
            wlr_scene_node_set_enabled(&view->scene_tree->node, false);
        }
    }
    
    /* Show current workspace views */
    int count = 0;
    struct view *views[2] = {NULL, NULL};
    wl_list_for_each(view, &output->workspaces[output->current_ws], ws_link) {
        if (count < 2) views[count++] = view;
    }
    
    if (count == 1 && views[0]->mapped) {
        wlr_scene_node_set_enabled(&views[0]->scene_tree->node, true);
        wlr_scene_node_set_position(&views[0]->scene_tree->node, 0, 0);
        wlr_xdg_toplevel_set_size(views[0]->xdg_toplevel, width, height);
    } else if (count == 2) {
        int half = width / 2;
        for (int i = 0; i < 2; i++) {
            if (views[i]->mapped) {
                wlr_scene_node_set_enabled(&views[i]->scene_tree->node, true);
                wlr_scene_node_set_position(&views[i]->scene_tree->node, i * half, 0);
                wlr_xdg_toplevel_set_size(views[i]->xdg_toplevel, half, height);
            }
        }
    }
}

static struct output *find_space(struct server *server, int *out_ws) {
    struct output *output;
    wl_list_for_each(output, &server->outputs, link) {
        for (int i = 0; i < server->config.num_workspaces; i++) {
            if (count_workspace_views(output, i) < VIEWS_PER_WS) {
                *out_ws = i;
                return output;
            }
        }
    }
    return NULL;
}

/* Execute command */
static void exec_command(const char *cmd) {
    if (fork() == 0) {
        setsid();
        execl("/bin/sh", "/bin/sh", "-lc", cmd, NULL);
        _exit(1);
    }
}

/* Focus cycling */
static void cycle_focus(struct server *server) {
    struct output *output;
    wl_list_for_each(output, &server->outputs, link) {
        struct workspace {
            struct view *views[2];
            int count;
        } ws = {{NULL, NULL}, 0};
        
        struct view *v;
        wl_list_for_each(v, &output->workspaces[output->current_ws], ws_link) {
            if (ws.count < 2 && v->mapped) {
                ws.views[ws.count++] = v;
            }
        }
        
        if (ws.count == 2) {
            struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
            struct view *other = NULL;
            
            if (ws.views[0]->xdg_toplevel->base->surface == focused) {
                other = ws.views[1];
            } else if (ws.views[1]->xdg_toplevel->base->surface == focused) {
                other = ws.views[0];
            } else {
                other = ws.views[0];
            }
            
            if (other && other->mapped) {
                wlr_seat_keyboard_notify_enter(server->seat,
                    other->xdg_toplevel->base->surface, NULL, 0, NULL);
                fprintf(stderr, "Focus cycled\n");
            }
        }
    }
}

/* Keyboard handling */
static void handle_key(struct server *server, xkb_keysym_t sym, uint32_t mods) {
    uint32_t ctrl_shift = WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT;
    
    /* Command box handling */
    if (server->cmdbox.active) {
        if (sym == XKB_KEY_Escape) {
            server->cmdbox.active = false;
            server->cmdbox.len = 0;
            server->cmdbox.text[0] = '\0';
            fprintf(stderr, "Command box closed\n");
        } else if (sym == XKB_KEY_Return) {
            if (server->cmdbox.len > 0) {
                fprintf(stderr, "Executing: %s\n", server->cmdbox.text);
                exec_command(server->cmdbox.text);
            }
            server->cmdbox.active = false;
            server->cmdbox.len = 0;
            server->cmdbox.text[0] = '\0';
        } else if (sym == XKB_KEY_BackSpace) {
            if (server->cmdbox.len > 0) {
                server->cmdbox.len--;
                server->cmdbox.text[server->cmdbox.len] = '\0';
                fprintf(stderr, "Command: %s\n", server->cmdbox.text);
            }
        } else if (sym >= 32 && sym < 127 && server->cmdbox.len < MAX_CMD_LEN - 1) {
            server->cmdbox.text[server->cmdbox.len++] = (char)sym;
            server->cmdbox.text[server->cmdbox.len] = '\0';
            fprintf(stderr, "Command: %s\n", server->cmdbox.text);
        }
        return;
    }
    
    /* Global shortcuts */
    if (mods != ctrl_shift) return;
    
    switch (sym) {
        case XKB_KEY_Down:
            fprintf(stderr, "Exiting ElDinWM\n");
            wl_display_terminate(server->display);
            break;
            
        case XKB_KEY_Left:
        case XKB_KEY_Right: {
            int delta = (sym == XKB_KEY_Right) ? 1 : -1;
            struct output *output;
            wl_list_for_each(output, &server->outputs, link) {
                int new_ws = output->current_ws + delta;
                if (new_ws >= 0 && new_ws < server->config.num_workspaces) {
                    output->current_ws = new_ws;
                    clock_gettime(CLOCK_MONOTONIC, &output->ws_switch_time);
                    layout_workspace(output);
                    fprintf(stderr, "Workspace: %d/%d\n", new_ws + 1, 
                        server->config.num_workspaces);
                }
            }
            break;
        }
        
        case XKB_KEY_z:
        case XKB_KEY_Z:
            server->cmdbox.active = true;
            server->cmdbox.len = 0;
            server->cmdbox.text[0] = '\0';
            fprintf(stderr, "Command box opened (type command, Enter to run, Esc to cancel)\n");
            break;
            
        case XKB_KEY_x:
        case XKB_KEY_X:
            cycle_focus(server);
            break;
    }
}

static void kb_modifiers(struct wl_listener *listener, void *data) {
    struct keyboard *kb = wl_container_of(listener, kb, modifiers);
    wlr_seat_set_keyboard(kb->server->seat, kb->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(kb->server->seat, &kb->wlr_keyboard->modifiers);
}

static void kb_key(struct wl_listener *listener, void *data) {
    struct keyboard *kb = wl_container_of(listener, kb, key);
    struct wlr_keyboard_key_event *event = data;
    
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        const xkb_keysym_t *syms;
        int nsyms = xkb_state_key_get_syms(kb->wlr_keyboard->xkb_state, 
            event->keycode + 8, &syms);
        uint32_t mods = wlr_keyboard_get_modifiers(kb->wlr_keyboard);
        
        for (int i = 0; i < nsyms; i++) {
            handle_key(kb->server, syms[i], mods);
        }
    }
    
    wlr_seat_set_keyboard(kb->server->seat, kb->wlr_keyboard);
    wlr_seat_keyboard_notify_key(kb->server->seat, event->time_msec,
        event->keycode, event->state);
}

static void new_keyboard(struct server *server, struct wlr_input_device *device) {
    struct wlr_keyboard *wlr_kb = wlr_keyboard_from_input_device(device);
    struct keyboard *kb = calloc(1, sizeof(*kb));
    kb->server = server;
    kb->wlr_keyboard = wlr_kb;
    
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(wlr_kb, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);
    wlr_keyboard_set_repeat_info(wlr_kb, 25, 600);
    
    kb->modifiers.notify = kb_modifiers;
    wl_signal_add(&wlr_kb->events.modifiers, &kb->modifiers);
    kb->key.notify = kb_key;
    wl_signal_add(&wlr_kb->events.key, &kb->key);
    
    wlr_seat_set_keyboard(server->seat, wlr_kb);
    wl_list_insert(&server->keyboards, &kb->link);
}

/* Cursor handling */
static void process_cursor_motion(struct server *server, uint32_t time) {
    wlr_seat_pointer_notify_motion(server->seat, time,
        server->cursor->x, server->cursor->y);
}

static void cursor_motion(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    process_cursor_motion(server, event->time_msec);
}

static void cursor_motion_absolute(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(server, event->time_msec);
}

static void cursor_button(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;
    wlr_seat_pointer_notify_button(server->seat, event->time_msec,
        event->button, event->state);
}

static void cursor_axis(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
        event->orientation, event->delta, event->delta_discrete, event->source,
        event->relative_direction);
}

static void cursor_frame(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}

static void request_cursor(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    if (server->seat->pointer_state.focused_client == event->seat_client) {
        wlr_cursor_set_surface(server->cursor, event->surface,
            event->hotspot_x, event->hotspot_y);
    }
}

static void request_set_selection(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static void new_pointer(struct server *server, struct wlr_input_device *device) {
    wlr_cursor_attach_input_device(server->cursor, device);
}

static void new_input(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;
    
    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            new_keyboard(server, device);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            new_pointer(server, device);
            break;
        default:
            break;
    }
    
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(server->seat, caps);
}

/* View handlers */
static void view_map(struct wl_listener *listener, void *data) {
    struct view *view = wl_container_of(listener, view, map);
    view->mapped = true;
    
    int ws;
    struct output *output = find_space(view->server, &ws);
    
    if (output) {
        view->output = output;
        view->workspace = ws;
        wl_list_insert(&output->workspaces[ws], &view->ws_link);
        layout_workspace(output);
        
        wlr_seat_keyboard_notify_enter(view->server->seat,
            view->xdg_toplevel->base->surface, NULL, 0, NULL);
        
        fprintf(stderr, "View mapped: workspace %d/%d (%d views)\n", 
            ws + 1, view->server->config.num_workspaces, 
            count_workspace_views(output, ws));
    } else {
        wlr_scene_node_set_enabled(&view->scene_tree->node, false);
        fprintf(stderr, "ElDinWM: All workspaces full (max 2 windows per workspace)\n");
    }
}

static void view_unmap(struct wl_listener *listener, void *data) {
    struct view *view = wl_container_of(listener, view, unmap);
    view->mapped = false;
    
    if (view->output) {
        wl_list_remove(&view->ws_link);
        layout_workspace(view->output);
        view->output = NULL;
        fprintf(stderr, "View unmapped\n");
    }
}

static void view_destroy(struct wl_listener *listener, void *data) {
    struct view *view = wl_container_of(listener, view, destroy);
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->request_fullscreen.link);
    wl_list_remove(&view->link);
    free(view);
}

static void view_request_fullscreen(struct wl_listener *listener, void *data) {
    struct view *view = wl_container_of(listener, view, request_fullscreen);
    wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, false);
}

static void new_xdg_surface(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, new_xdg_surface);
    struct wlr_xdg_surface *xdg_surface = data;
    
    if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) return;
    
    struct view *view = calloc(1, sizeof(*view));
    view->server = server;
    view->xdg_toplevel = xdg_surface->toplevel;
    view->scene_tree = wlr_scene_xdg_surface_create(&server->scene->tree, xdg_surface);
    wl_list_init(&view->ws_link);
    
    view->map.notify = view_map;
    wl_signal_add(&xdg_surface->surface->events.map, &view->map);
    view->unmap.notify = view_unmap;
    wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);
    view->destroy.notify = view_destroy;
    wl_signal_add(&xdg_surface->events.destroy, &view->destroy);
    view->request_fullscreen.notify = view_request_fullscreen;
    wl_signal_add(&xdg_surface->toplevel->events.request_fullscreen, &view->request_fullscreen);
    
    wl_list_insert(&server->views, &view->link);
}

/* Output handlers */
static void output_frame(struct wl_listener *listener, void *data) {
    struct output *output = wl_container_of(listener, output, frame);
    struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
        output->server->scene, output->wlr_output);
    
    wlr_scene_output_commit(scene_output, NULL);
    
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_destroy(struct wl_listener *listener, void *data) {
    struct output *output = wl_container_of(listener, output, destroy);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    free(output);
}

static void new_output(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;
    
    wlr_output_init_render(wlr_output, server->allocator, server->renderer);
    
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode) wlr_output_state_set_mode(&state, mode);
    
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);
    
    struct output *output = calloc(1, sizeof(*output));
    output->server = server;
    output->wlr_output = wlr_output;
    output->current_ws = 0;
    
    for (int i = 0; i < MAX_WORKSPACES; i++) {
        wl_list_init(&output->workspaces[i]);
    }
    
    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);
    
    wlr_output_layout_add_auto(server->output_layout, wlr_output);
    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
    
    wl_list_insert(&server->outputs, &output->link);
    fprintf(stderr, "Output: %s (%dx%d)\n", wlr_output->name,
        wlr_output->width, wlr_output->height);
}

/* Config parsing */
static void get_config_path(char *path, size_t size) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg) {
        snprintf(path, size, "%s/eldinwm/eldinwm.conf", xdg);
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
    if (start != str) memmove(str, start, strlen(start) + 1);
}

static void parse_config(struct config *cfg, const char *path) {
    cfg->num_workspaces = 4;
    cfg->background_image[0] = '\0';
    
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Config: using defaults (4 workspaces)\n");
        return;
    }
    
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        trim(line);
        if (!line[0]) continue;
        
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        
        char *key = line;
        char *val = eq + 1;
        trim(key);
        trim(val);
        
        if (val[0] == '"') {
            val++;
            char *end = strchr(val, '"');
            if (end) *end = '\0';
        }
        
        if (strcmp(key, "workspaces") == 0) {
            int ws = atoi(val);
            if (ws >= 1 && ws <= MAX_WORKSPACES) {
                cfg->num_workspaces = ws;
            }
        } else if (strcmp(key, "background_image") == 0) {
            strncpy(cfg->background_image, val, sizeof(cfg->background_image) - 1);
        }
    }
    
    fclose(f);
    fprintf(stderr, "Config: %d workspaces\n", cfg->num_workspaces);
}

static void handle_signal(int sig) {
    wl_display_terminate(g_server.display);
}

int main(void) {
    wlr_log_init(WLR_ERROR, NULL);
    
    struct server *s = &g_server;
    wl_list_init(&s->outputs);
    wl_list_init(&s->views);
    wl_list_init(&s->keyboards);
    
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGCHLD, SIG_IGN);
    
    /* Load config */
    char cfg_path[512];
    get_config_path(cfg_path, sizeof(cfg_path));
    parse_config(&s->config, cfg_path);
    
    s->display = wl_display_create();
    s->backend = wlr_backend_autocreate(wl_display_get_event_loop(s->display), NULL);
    s->renderer = wlr_renderer_autocreate(s->backend);
    wlr_renderer_init_wl_display(s->renderer, s->display);
    s->allocator = wlr_allocator_autocreate(s->backend, s->renderer);
    
    wlr_compositor_create(s->display, 5, s->renderer);
    wlr_data_device_manager_create(s->display);
    
    s->output_layout = wlr_output_layout_create(s->display);
    s->scene = wlr_scene_create();
    s->scene_layout = wlr_scene_attach_output_layout(s->scene, s->output_layout);
    
    /* Background (dark blue) */
    float bg[4] = {0.0, 0.05, 0.15, 1.0};
    wlr_scene_rect_create(&s->scene->tree, 8192, 8192, bg);
    
    s->xdg_shell = wlr_xdg_shell_create(s->display, 3);
    s->new_xdg_surface.notify = new_xdg_surface;
    wl_signal_add(&s->xdg_shell->events.new_surface, &s->new_xdg_surface);
    
    /* Cursor */
    s->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(s->cursor, s->output_layout);
    s->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    
    s->cursor_motion.notify = cursor_motion;
    wl_signal_add(&s->cursor->events.motion, &s->cursor_motion);
    s->cursor_motion_absolute.notify = cursor_motion_absolute;
    wl_signal_add(&s->cursor->events.motion_absolute, &s->cursor_motion_absolute);
    s->cursor_button.notify = cursor_button;
    wl_signal_add(&s->cursor->events.button, &s->cursor_button);
    s->cursor_axis.notify = cursor_axis;
    wl_signal_add(&s->cursor->events.axis, &s->cursor_axis);
    s->cursor_frame.notify = cursor_frame;
    wl_signal_add(&s->cursor->events.frame, &s->cursor_frame);
    
    s->seat = wlr_seat_create(s->display, "seat0");
    s->request_cursor.notify = request_cursor;
    wl_signal_add(&s->seat->events.request_set_cursor, &s->request_cursor);
    s->request_set_selection.notify = request_set_selection;
    wl_signal_add(&s->seat->events.request_set_selection, &s->request_set_selection);
    
    s->new_input.notify = new_input;
    wl_signal_add(&s->backend->events.new_input, &s->new_input);
    
    s->new_output.notify = new_output;
    wl_signal_add(&s->backend->events.new_output, &s->new_output);
    
    const char *socket = wl_display_add_socket_auto(s->display);
    if (!socket || !wlr_backend_start(s->backend)) {
        fprintf(stderr, "Failed to start\n");
        return 1;
    }
    
    setenv("WAYLAND_DISPLAY", socket, 1);
    
    fprintf(stderr, "\n╔══════════════════════════════════════╗\n");
    fprintf(stderr, "║         ElDinWM Started              ║\n");
    fprintf(stderr, "╚══════════════════════════════════════╝\n\n");
    fprintf(stderr, "WAYLAND_DISPLAY=%s\n", socket);
    fprintf(stderr, "Workspaces: %d\n", s->config.num_workspaces);
    fprintf(stderr, "Max windows per workspace: 2\n");
    fprintf(stderr, "\nKeybindings:\n");
    fprintf(stderr, "  Ctrl+Shift+Left/Right  - Switch workspace\n");
    fprintf(stderr, "  Ctrl+Shift+Z           - Open command box\n");
    fprintf(stderr, "  Ctrl+Shift+X           - Cycle focus (2 windows)\n");
    fprintf(stderr, "  Ctrl+Shift+Down        - Exit\n");
    fprintf(stderr, "\nConfig: %s\n\n", cfg_path);
    
    s->running = true;
    wl_display_run(s->display);
    
    wl_display_destroy(s->display);
    fprintf(stderr, "\nElDinWM: Clean exit\n");
    return 0;
}
