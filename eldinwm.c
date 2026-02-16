/*
 * ElDinWM - Minimalist Wayland Tiling Compositor
 * Pure C, wlroots 0.18 compatible
 * All features: tiling, workspaces, command box, focus cycling
 */

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
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
#define VIEWS_PER_WORKSPACE 2
#define MAX_COMMAND_LENGTH 512
#define INDICATOR_HEIGHT 28

/* Configuration */
struct config {
    int num_workspaces;
    char background_image[512];
};

/* Forward declarations */
struct server;
struct output;
struct view;

/* View represents a window */
struct view {
    struct wl_list link;
    struct server *server;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree *scene_tree;
    
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener request_fullscreen;
    
    struct output *output;
    int workspace_idx;
    bool mapped;
};

/* Workspace holds up to 2 views */
struct workspace {
    struct wl_list views; /* view::link */
    int view_count;
};

/* Output represents a monitor */
struct output {
    struct wl_list link;
    struct server *server;
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;
    
    struct wl_listener frame;
    struct wl_listener destroy_output;
    
    int current_workspace;
    struct workspace workspaces[MAX_WORKSPACES];
};

/* Keyboard */
struct keyboard {
    struct wl_list link;
    struct server *server;
    struct wlr_keyboard *wlr_keyboard;
    
    struct wl_listener modifiers;
    struct wl_listener key;
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
    
    struct wl_list outputs; /* output::link */
    struct wl_list views;   /* view::link */
    struct wl_list keyboards; /* keyboard::link */
    
    struct config config;
    struct command_box command_box;
    bool running;
};

static struct server g_server = {0};

/* Layout computation */
static void relayout_workspace(struct output *output) {
    if (!output || !output->wlr_output) return;
    
    struct workspace *ws = &output->workspaces[output->current_workspace];
    int width = output->wlr_output->width;
    int height = output->wlr_output->height;
    int usable_height = height - INDICATOR_HEIGHT;
    
    /* Hide all views on this output first */
    struct view *view;
    wl_list_for_each(view, &output->server->views, link) {
        if (view->mapped) {
            wlr_scene_node_set_enabled(&view->scene_tree->node, false);
        }
    }
    
    /* Show and position views in current workspace */
    if (ws->view_count == 1) {
        /* Fullscreen */
        struct view *v = wl_container_of(ws->views.next, v, link);
        if (v->mapped) {
            wlr_scene_node_set_enabled(&v->scene_tree->node, true);
            wlr_scene_node_set_position(&v->scene_tree->node, 0, INDICATOR_HEIGHT);
            wlr_xdg_toplevel_set_size(v->xdg_toplevel, width, usable_height);
        }
    } else if (ws->view_count == 2) {
        /* 50/50 split */
        int half_width = width / 2;
        int i = 0;
        wl_list_for_each(view, &ws->views, link) {
            if (view->mapped) {
                wlr_scene_node_set_enabled(&view->scene_tree->node, true);
                wlr_scene_node_set_position(&view->scene_tree->node,
                    i == 0 ? 0 : half_width, INDICATOR_HEIGHT);
                wlr_xdg_toplevel_set_size(view->xdg_toplevel, half_width, usable_height);
            }
            i++;
        }
    }
}

/* Find available workspace slot */
static bool find_available_workspace(struct server *server, 
                                     struct output **out_output, int *out_ws) {
    struct output *output;
    wl_list_for_each(output, &server->outputs, link) {
        for (int i = 0; i < server->config.num_workspaces; i++) {
            if (output->workspaces[i].view_count < VIEWS_PER_WORKSPACE) {
                *out_output = output;
                *out_ws = i;
                return true;
            }
        }
    }
    return false;
}

/* Add view to workspace */
static void add_view_to_workspace(struct output *output, int ws_idx, struct view *view) {
    struct workspace *ws = &output->workspaces[ws_idx];
    wl_list_insert(&ws->views, &view->link);
    ws->view_count++;
    view->output = output;
    view->workspace_idx = ws_idx;
}

/* Remove view from workspace */
static void remove_view_from_workspace(struct view *view) {
    if (!view->output) return;
    
    struct workspace *ws = &view->output->workspaces[view->workspace_idx];
    wl_list_remove(&view->link);
    ws->view_count--;
    
    relayout_workspace(view->output);
    view->output = NULL;
}

/* Execute shell command */
static void execute_command(const char *cmd) {
    if (fork() == 0) {
        setsid();
        execl("/bin/sh", "/bin/sh", "-lc", cmd, NULL);
        _exit(1);
    }
}

/* Keyboard handling */
static void handle_keybinding(struct server *server, xkb_keysym_t sym, uint32_t mods) {
    const uint32_t CTRL_SHIFT = WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT;
    
    /* Command box active - handle text input */
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
        fprintf(stderr, "Command box: %s\n", server->command_box.text);
        return;
    }
    
    /* Global shortcuts */
    if (mods == CTRL_SHIFT) {
        switch (sym) {
            case XKB_KEY_Down:
                fprintf(stderr, "ElDinWM: Exiting via Ctrl+Shift+Down\n");
                wl_display_terminate(server->wl_display);
                server->running = false;
                break;
                
            case XKB_KEY_Left:
            case XKB_KEY_Right: {
                int delta = (sym == XKB_KEY_Right) ? 1 : -1;
                struct output *output;
                wl_list_for_each(output, &server->outputs, link) {
                    int new_ws = output->current_workspace + delta;
                    if (new_ws >= 0 && new_ws < server->config.num_workspaces) {
                        output->current_workspace = new_ws;
                        relayout_workspace(output);
                        fprintf(stderr, "Switched to workspace %d\n", new_ws + 1);
                    }
                }
                break;
            }
            
            case XKB_KEY_z:
            case XKB_KEY_Z:
                server->command_box.active = true;
                server->command_box.length = 0;
                server->command_box.text[0] = '\0';
                fprintf(stderr, "Command box opened\n");
                break;
                
            case XKB_KEY_x:
            case XKB_KEY_X: {
                struct output *output;
                wl_list_for_each(output, &server->outputs, link) {
                    struct workspace *ws = &output->workspaces[output->current_workspace];
                    if (ws->view_count == 2) {
                        struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
                        struct view *other = NULL;
                        struct view *v;
                        wl_list_for_each(v, &ws->views, link) {
                            if (v->xdg_toplevel->base->surface != focused) {
                                other = v;
                                break;
                            }
                        }
                        if (other && other->mapped) {
                            wlr_seat_keyboard_notify_enter(server->seat,
                                other->xdg_toplevel->base->surface, NULL, 0, NULL);
                            fprintf(stderr, "Focus cycled\n");
                        }
                    }
                }
                break;
            }
        }
    }
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
    struct keyboard *kb = wl_container_of(listener, kb, modifiers);
    wlr_seat_set_keyboard(kb->server->seat, kb->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(kb->server->seat, &kb->wlr_keyboard->modifiers);
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
    struct keyboard *kb = wl_container_of(listener, kb, key);
    struct wlr_keyboard_key_event *event = data;
    
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        uint32_t keycode = event->keycode + 8;
        const xkb_keysym_t *syms;
        int nsyms = xkb_state_key_get_syms(kb->wlr_keyboard->xkb_state, keycode, &syms);
        uint32_t mods = wlr_keyboard_get_modifiers(kb->wlr_keyboard);
        
        for (int i = 0; i < nsyms; i++) {
            handle_keybinding(kb->server, syms[i], mods);
        }
    }
    
    wlr_seat_set_keyboard(kb->server->seat, kb->wlr_keyboard);
    wlr_seat_keyboard_notify_key(kb->server->seat, event->time_msec,
        event->keycode, event->state);
}

static void server_new_keyboard(struct server *server, struct wlr_input_device *device) {
    struct wlr_keyboard *wlr_kb = wlr_keyboard_from_input_device(device);
    
    struct keyboard *kb = calloc(1, sizeof(struct keyboard));
    kb->server = server;
    kb->wlr_keyboard = wlr_kb;
    
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(wlr_kb, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(wlr_kb, 25, 600);
    
    kb->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_kb->events.modifiers, &kb->modifiers);
    
    kb->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_kb->events.key, &kb->key);
    
    wlr_seat_set_keyboard(server->seat, wlr_kb);
    wl_list_insert(&server->keyboards, &kb->link);
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
    if (server->seat->pointer_state.focused_client == event->seat_client) {
        wlr_cursor_set_surface(server->cursor, event->surface,
            event->hotspot_x, event->hotspot_y);
    }
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(server->seat, event->source, event->serial);
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
        fprintf(stderr, "Window mapped to workspace %d\n", ws_idx + 1);
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
    wl_list_remove(&view->request_fullscreen.link);
    
    free(view);
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
    
    view->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
    wl_signal_add(&xdg_surface->toplevel->events.request_fullscreen, &view->request_fullscreen);
    
    wl_list_insert(&server->views, &view->link);
}

/* Output rendering */
static void output_frame(struct wl_listener *listener, void *data) {
    struct output *output = wl_container_of(listener, output, frame);
    struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
        output->server->scene, output->wlr_output);
    
    wlr_scene_output_commit(scene_output, NULL);
    
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_destroy_notify(struct wl_listener *listener, void *data) {
    struct output *output = wl_container_of(listener, output, destroy_output);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->destroy_output.link);
    wl_list_remove(&output->link);
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
    
    struct output *output = calloc(1, sizeof(struct output));
    output->server = server;
    output->wlr_output = wlr_output;
    
    /* Initialize workspaces */
    for (int i = 0; i < server->config.num_workspaces; i++) {
        wl_list_init(&output->workspaces[i].views);
        output->workspaces[i].view_count = 0;
    }
    
    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    
    output->destroy_output.notify = output_destroy_notify;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy_output);
    
    wlr_output_layout_add_auto(server->output_layout, wlr_output);
    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
    
    wl_list_insert(&server->outputs, &output->link);
    
    fprintf(stderr, "ElDinWM: Output added\n");
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
    config->num_workspaces = 4;
    config->background_image[0] = '\0';
    
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "ElDinWM: Using defaults (4 workspaces)\n");
        return;
    }
    
    char line[1024];
    while (fgets(line, sizeof(line), file)) {
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
        
        if (value[0] == '"') {
            value++;
            char *end_quote = strchr(value, '"');
            if (end_quote) *end_quote = '\0';
        }
        
        if (strcmp(key, "workspaces") == 0) {
            int ws = atoi(value);
            if (ws >= 1 && ws <= MAX_WORKSPACES) {
                config->num_workspaces = ws;
            }
        } else if (strcmp(key, "background_image") == 0) {
            strncpy(config->background_image, value, sizeof(config->background_image) - 1);
        }
    }
    
    fclose(file);
}

/* Signal handler */
static void handle_signal(int sig) {
    wl_display_terminate(g_server.wl_display);
    g_server.running = false;
}

/* Main */
int main(void) {
    wlr_log_init(WLR_ERROR, NULL);
    
    struct server *server = &g_server;
    wl_list_init(&server->outputs);
    wl_list_init(&server->views);
    wl_list_init(&server->keyboards);
    
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGCHLD, SIG_IGN);
    
    /* Parse config */
    char config_path[512];
    get_config_path(config_path, sizeof(config_path));
    parse_config(&server->config, config_path);
    
    fprintf(stderr, "ElDinWM: Starting with %d workspaces\n", server->config.num_workspaces);
    
    server->wl_display = wl_display_create();
    server->backend = wlr_backend_autocreate(wl_display_get_event_loop(server->wl_display), NULL);
    server->renderer = wlr_renderer_autocreate(server->backend);
    wlr_renderer_init_wl_display(server->renderer, server->wl_display);
    
    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    
    wlr_compositor_create(server->wl_display, 5, server->renderer);
    wlr_data_device_manager_create(server->wl_display);
    
    server->output_layout = wlr_output_layout_create(server->wl_display);
    server->scene = wlr_scene_create();
    wlr_scene_attach_output_layout(server->scene, server->output_layout);
    
    server->xdg_shell = wlr_xdg_shell_create(server->wl_display, 3);
    server->new_xdg_surface.notify = server_new_xdg_surface;
    wl_signal_add(&server->xdg_shell->events.new_surface, &server->new_xdg_surface);
    
    /* Cursor */
    server->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
    server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    
    server->cursor_motion.notify = server_cursor_motion;
    wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
    
    server->cursor_motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&server->cursor->events.motion_absolute, &server->cursor_motion_absolute);
    
    server->cursor_button.notify = server_cursor_button;
    wl_signal_add(&server->cursor->events.button, &server->cursor_button);
    
    server->cursor_axis.notify = server_cursor_axis;
    wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
    
    server->cursor_frame.notify = server_cursor_frame;
    wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);
    
    /* Seat */
    server->seat = wlr_seat_create(server->wl_display, "seat0");
    server->request_cursor.notify = seat_request_cursor;
    wl_signal_add(&server->seat->events.request_set_cursor, &server->request_cursor);
    
    server->request_set_selection.notify = seat_request_set_selection;
    wl_signal_add(&server->seat->events.request_set_selection, &server->request_set_selection);
    
    server->new_input.notify = server_new_input;
    wl_signal_add(&server->backend->events.new_input, &server->new_input);
    
    server->new_output.notify = server_new_output;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);
    
    const char *socket = wl_display_add_socket_auto(server->wl_display);
    if (!socket || !wlr_backend_start(server->backend)) {
        fprintf(stderr, "ElDinWM: Failed to start\n");
        return 1;
    }
    
    setenv("WAYLAND_DISPLAY", socket, 1);
    fprintf(stderr, "ElDinWM: Running on WAYLAND_DISPLAY=%s\n", socket);
    fprintf(stderr, "ElDinWM: Keybindings:\n");
    fprintf(stderr, "  Ctrl+Shift+Left/Right - Switch workspace\n");
    fprintf(stderr, "  Ctrl+Shift+Z - Open command box\n");
    fprintf(stderr, "  Ctrl+Shift+X - Cycle focus (when 2 windows)\n");
    fprintf(stderr, "  Ctrl+Shift+Down - Exit\n");
    
    server->running = true;
    wl_display_run(server->wl_display);
    
    wl_display_destroy(server->wl_display);
    fprintf(stderr, "ElDinWM: Exited cleanly\n");
    return 0;
}
