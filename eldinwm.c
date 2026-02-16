/*
 * ElDinWM - Array-based version (no linked list iterations)
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
#define MAX_VIEWS 64
#define MAX_OUTPUTS 8
#define MAX_KEYBOARDS 8
#define VIEWS_PER_WS 2
#define MAX_CMD_LEN 512

struct server;
struct output;
struct view;

/* Simple command box */
struct cmdbox {
    bool active;
    char text[MAX_CMD_LEN];
    int len;
};

/* Server with arrays instead of lists */
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
    
    struct output *outputs[MAX_OUTPUTS];
    int output_count;
    
    struct view *views[MAX_VIEWS];
    int view_count;
    
    struct keyboard *keyboards[MAX_KEYBOARDS];
    int keyboard_count;
    
    int num_workspaces;
    struct cmdbox cmdbox;
    bool running;
};

/* Output */
struct output {
    struct server *server;
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;
    struct wl_listener frame;
    struct wl_listener destroy;
    
    int current_ws;
    struct view *workspaces[MAX_WORKSPACES][VIEWS_PER_WS];
};

/* View */
struct view {
    struct server *server;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree *scene_tree;
    
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    
    struct output *output;
    int workspace;
    int ws_slot;
    bool mapped;
};

/* Keyboard */
struct keyboard {
    struct server *server;
    struct wlr_keyboard *wlr_keyboard;
    struct wl_listener modifiers;
    struct wl_listener key;
};

static struct server g_server = {0};

/* Find space in workspaces */
static bool find_space(struct server *s, struct output **out_output, int *out_ws, int *out_slot) {
    for (int i = 0; i < s->output_count; i++) {
        struct output *o = s->outputs[i];
        if (!o) continue;
        
        for (int ws = 0; ws < s->num_workspaces; ws++) {
            for (int slot = 0; slot < VIEWS_PER_WS; slot++) {
                if (o->workspaces[ws][slot] == NULL) {
                    *out_output = o;
                    *out_ws = ws;
                    *out_slot = slot;
                    return true;
                }
            }
        }
    }
    return false;
}

/* Layout workspace */
static void layout_workspace(struct output *output) {
    if (!output || !output->wlr_output) return;
    
    int width = output->wlr_output->width;
    int height = output->wlr_output->height;
    int ws = output->current_ws;
    
    fprintf(stderr, "[LAYOUT] Workspace %d\n", ws + 1);
    
    /* Hide all views first */
    for (int i = 0; i < output->server->view_count; i++) {
        struct view *v = output->server->views[i];
        if (v && v->mapped && v->scene_tree) {
            wlr_scene_node_set_enabled(&v->scene_tree->node, false);
        }
    }
    
    /* Count views in current workspace */
    int count = 0;
    struct view *visible[2] = {NULL, NULL};
    
    for (int slot = 0; slot < VIEWS_PER_WS; slot++) {
        struct view *v = output->workspaces[ws][slot];
        if (v && v->mapped) {
            visible[count++] = v;
        }
    }
    
    fprintf(stderr, "[LAYOUT] %d visible views\n", count);
    
    /* Layout */
    if (count == 1 && visible[0]) {
        wlr_scene_node_set_enabled(&visible[0]->scene_tree->node, true);
        wlr_scene_node_set_position(&visible[0]->scene_tree->node, 0, 0);
        wlr_xdg_toplevel_set_size(visible[0]->xdg_toplevel, width, height);
    } else if (count == 2) {
        int half = width / 2;
        for (int i = 0; i < 2; i++) {
            if (visible[i]) {
                wlr_scene_node_set_enabled(&visible[i]->scene_tree->node, true);
                wlr_scene_node_set_position(&visible[i]->scene_tree->node, i * half, 0);
                wlr_xdg_toplevel_set_size(visible[i]->xdg_toplevel, half, height);
            }
        }
    }
}

static void exec_command(const char *cmd) {
    if (!cmd || !cmd[0]) return;
    if (fork() == 0) {
        setsid();
        execl("/bin/sh", "/bin/sh", "-lc", cmd, NULL);
        _exit(1);
    }
}

static void cycle_focus(struct server *s) {
    fprintf(stderr, "[FOCUS] Cycling\n");
    
    for (int i = 0; i < s->output_count; i++) {
        struct output *o = s->outputs[i];
        if (!o) continue;
        
        int ws = o->current_ws;
        struct view *v0 = o->workspaces[ws][0];
        struct view *v1 = o->workspaces[ws][1];
        
        if (v0 && v1 && v0->mapped && v1->mapped) {
            struct wlr_surface *focused = s->seat->keyboard_state.focused_surface;
            struct view *other = (v0->xdg_toplevel->base->surface == focused) ? v1 : v0;
            
            wlr_seat_keyboard_notify_enter(s->seat, other->xdg_toplevel->base->surface, NULL, 0, NULL);
            fprintf(stderr, "[FOCUS] Cycled\n");
        }
    }
}

static void handle_key(struct server *s, xkb_keysym_t sym, uint32_t mods) {
    uint32_t ctrl_shift = WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT;
    
    /* Command box mode */
    if (s->cmdbox.active) {
        fprintf(stderr, "[CMDBOX] Key: %d\n", sym);
        
        if (sym == XKB_KEY_Escape) {
            s->cmdbox.active = false;
            s->cmdbox.len = 0;
            s->cmdbox.text[0] = '\0';
            fprintf(stderr, "[CMDBOX] Closed\n");
        } else if (sym == XKB_KEY_Return) {
            if (s->cmdbox.len > 0) {
                fprintf(stderr, "[CMDBOX] Exec: %s\n", s->cmdbox.text);
                exec_command(s->cmdbox.text);
            }
            s->cmdbox.active = false;
            s->cmdbox.len = 0;
            s->cmdbox.text[0] = '\0';
        } else if (sym == XKB_KEY_BackSpace) {
            if (s->cmdbox.len > 0) {
                s->cmdbox.len--;
                s->cmdbox.text[s->cmdbox.len] = '\0';
                fprintf(stderr, "[CMDBOX] %s\n", s->cmdbox.text);
            }
        } else if (sym >= 32 && sym < 127 && s->cmdbox.len < MAX_CMD_LEN - 1) {
            s->cmdbox.text[s->cmdbox.len++] = (char)sym;
            s->cmdbox.text[s->cmdbox.len] = '\0';
            fprintf(stderr, "[CMDBOX] %s\n", s->cmdbox.text);
        }
        return;
    }
    
    /* Global shortcuts */
    if (mods != ctrl_shift) return;
    
    switch (sym) {
        case XKB_KEY_Down:
            fprintf(stderr, "[EXIT] Bye\n");
            wl_display_terminate(s->display);
            s->running = false;
            break;
            
        case XKB_KEY_Left:
        case XKB_KEY_Right: {
            fprintf(stderr, "[WORKSPACE] Switch attempt (outputs: %d)\n", s->output_count);
            
            int delta = (sym == XKB_KEY_Right) ? 1 : -1;
            
            for (int i = 0; i < s->output_count; i++) {
                struct output *o = s->outputs[i];
                if (!o) {
                    fprintf(stderr, "[WORKSPACE] Output %d is NULL\n", i);
                    continue;
                }
                
                int new_ws = o->current_ws + delta;
                fprintf(stderr, "[WORKSPACE] Output %d: %d -> %d\n", i, o->current_ws, new_ws);
                
                if (new_ws >= 0 && new_ws < s->num_workspaces) {
                    o->current_ws = new_ws;
                    fprintf(stderr, "[WORKSPACE] Switched to %d, calling layout\n", new_ws + 1);
                    layout_workspace(o);
                    fprintf(stderr, "[WORKSPACE] Layout done\n");
                } else {
                    fprintf(stderr, "[WORKSPACE] Out of bounds\n");
                }
            }
            
            fprintf(stderr, "[WORKSPACE] Switch complete\n");
            break;
        }
        
        case XKB_KEY_z:
        case XKB_KEY_Z:
            fprintf(stderr, "[CMDBOX] Opening\n");
            s->cmdbox.active = true;
            s->cmdbox.len = 0;
            s->cmdbox.text[0] = '\0';
            fprintf(stderr, "[CMDBOX] Ready\n");
            break;
            
        case XKB_KEY_x:
        case XKB_KEY_X:
            cycle_focus(s);
            break;
    }
    
    fprintf(stderr, "[KEY] Handler done\n");
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

static void new_keyboard(struct server *s, struct wlr_input_device *device) {
    if (s->keyboard_count >= MAX_KEYBOARDS) return;
    
    struct wlr_keyboard *wlr_kb = wlr_keyboard_from_input_device(device);
    struct keyboard *kb = calloc(1, sizeof(*kb));
    kb->server = s;
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
    
    wlr_seat_set_keyboard(s->seat, wlr_kb);
    s->keyboards[s->keyboard_count++] = kb;
    
    fprintf(stderr, "[INPUT] Keyboard added (%d total)\n", s->keyboard_count);
}

static void process_cursor_motion(struct server *s, uint32_t time) {
    wlr_seat_pointer_notify_motion(s->seat, time, s->cursor->x, s->cursor->y);
}

static void cursor_motion(struct wl_listener *listener, void *data) {
    struct server *s = wl_container_of(listener, s, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    wlr_cursor_move(s->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    process_cursor_motion(s, event->time_msec);
}

static void cursor_motion_absolute(struct wl_listener *listener, void *data) {
    struct server *s = wl_container_of(listener, s, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    wlr_cursor_warp_absolute(s->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(s, event->time_msec);
}

static void cursor_button(struct wl_listener *listener, void *data) {
    struct server *s = wl_container_of(listener, s, cursor_button);
    struct wlr_pointer_button_event *event = data;
    wlr_seat_pointer_notify_button(s->seat, event->time_msec, event->button, event->state);
}

static void cursor_axis(struct wl_listener *listener, void *data) {
    struct server *s = wl_container_of(listener, s, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    wlr_seat_pointer_notify_axis(s->seat, event->time_msec,
        event->orientation, event->delta, event->delta_discrete, event->source,
        event->relative_direction);
}

static void cursor_frame(struct wl_listener *listener, void *data) {
    struct server *s = wl_container_of(listener, s, cursor_frame);
    wlr_seat_pointer_notify_frame(s->seat);
}

static void request_cursor(struct wl_listener *listener, void *data) {
    struct server *s = wl_container_of(listener, s, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    if (s->seat->pointer_state.focused_client == event->seat_client) {
        wlr_cursor_set_surface(s->cursor, event->surface, event->hotspot_x, event->hotspot_y);
    }
}

static void request_set_selection(struct wl_listener *listener, void *data) {
    struct server *s = wl_container_of(listener, s, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(s->seat, event->source, event->serial);
}

static void new_pointer(struct server *s, struct wlr_input_device *device) {
    wlr_cursor_attach_input_device(s->cursor, device);
}

static void new_input(struct wl_listener *listener, void *data) {
    struct server *s = wl_container_of(listener, s, new_input);
    struct wlr_input_device *device = data;
    
    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            new_keyboard(s, device);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            new_pointer(s, device);
            break;
        default:
            break;
    }
    
    wlr_seat_set_capabilities(s->seat, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
}

static void view_map(struct wl_listener *listener, void *data) {
    struct view *view = wl_container_of(listener, view, map);
    view->mapped = true;
    
    struct output *output;
    int ws, slot;
    
    if (find_space(view->server, &output, &ws, &slot)) {
        view->output = output;
        view->workspace = ws;
        view->ws_slot = slot;
        output->workspaces[ws][slot] = view;
        
        layout_workspace(output);
        wlr_seat_keyboard_notify_enter(view->server->seat,
            view->xdg_toplevel->base->surface, NULL, 0, NULL);
        
        fprintf(stderr, "[VIEW] Mapped to WS %d slot %d\n", ws + 1, slot);
    } else {
        wlr_scene_node_set_enabled(&view->scene_tree->node, false);
        fprintf(stderr, "[VIEW] All workspaces full\n");
    }
}

static void view_unmap(struct wl_listener *listener, void *data) {
    struct view *view = wl_container_of(listener, view, unmap);
    view->mapped = false;
    
    if (view->output) {
        view->output->workspaces[view->workspace][view->ws_slot] = NULL;
        layout_workspace(view->output);
        view->output = NULL;
    }
}

static void view_destroy(struct wl_listener *listener, void *data) {
    struct view *view = wl_container_of(listener, view, destroy);
    
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);
    
    /* Remove from views array */
    for (int i = 0; i < view->server->view_count; i++) {
        if (view->server->views[i] == view) {
            view->server->views[i] = NULL;
            break;
        }
    }
    
    free(view);
}

static void new_xdg_surface(struct wl_listener *listener, void *data) {
    struct server *s = wl_container_of(listener, s, new_xdg_surface);
    struct wlr_xdg_surface *xdg_surface = data;
    
    if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) return;
    if (s->view_count >= MAX_VIEWS) return;
    
    struct view *view = calloc(1, sizeof(*view));
    view->server = s;
    view->xdg_toplevel = xdg_surface->toplevel;
    view->scene_tree = wlr_scene_xdg_surface_create(&s->scene->tree, xdg_surface);
    
    view->map.notify = view_map;
    wl_signal_add(&xdg_surface->surface->events.map, &view->map);
    view->unmap.notify = view_unmap;
    wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);
    view->destroy.notify = view_destroy;
    wl_signal_add(&xdg_surface->events.destroy, &view->destroy);
    
    s->views[s->view_count++] = view;
}

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
    
    /* Remove from array */
    for (int i = 0; i < output->server->output_count; i++) {
        if (output->server->outputs[i] == output) {
            output->server->outputs[i] = NULL;
            break;
        }
    }
    
    free(output);
}

static void new_output(struct wl_listener *listener, void *data) {
    struct server *s = wl_container_of(listener, s, new_output);
    struct wlr_output *wlr_output = data;
    
    if (s->output_count >= MAX_OUTPUTS) return;
    
    wlr_output_init_render(wlr_output, s->allocator, s->renderer);
    
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode) wlr_output_state_set_mode(&state, mode);
    
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);
    
    struct output *output = calloc(1, sizeof(*output));
    output->server = s;
    output->wlr_output = wlr_output;
    output->current_ws = 0;
    
    /* Initialize workspace array to NULL */
    for (int i = 0; i < MAX_WORKSPACES; i++) {
        for (int j = 0; j < VIEWS_PER_WS; j++) {
            output->workspaces[i][j] = NULL;
        }
    }
    
    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);
    
    wlr_output_layout_add_auto(s->output_layout, wlr_output);
    output->scene_output = wlr_scene_output_create(s->scene, wlr_output);
    
    s->outputs[s->output_count++] = output;
    
    fprintf(stderr, "[OUTPUT] %s added (%d total)\n", wlr_output->name, s->output_count);
}

static void handle_signal(int sig) {
    wl_display_terminate(g_server.display);
}

int main(void) {
    wlr_log_init(WLR_ERROR, NULL);
    
    struct server *s = &g_server;
    s->num_workspaces = 4;
    s->output_count = 0;
    s->view_count = 0;
    s->keyboard_count = 0;
    
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGCHLD, SIG_IGN);
    
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
    
    /* Background */
    struct wlr_scene_tree *bg = wlr_scene_tree_create(&s->scene->tree);
    wlr_scene_node_lower_to_bottom(&bg->node);
    float col[4] = {0.0, 0.05, 0.15, 1.0};
    wlr_scene_rect_create(bg, 8192, 8192, col);
    
    s->xdg_shell = wlr_xdg_shell_create(s->display, 3);
    s->new_xdg_surface.notify = new_xdg_surface;
    wl_signal_add(&s->xdg_shell->events.new_surface, &s->new_xdg_surface);
    
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
    
    fprintf(stderr, "\n");
    fprintf(stderr, "══════════════════════════════════════\n");
    fprintf(stderr, "       ElDinWM - Ready                \n");
    fprintf(stderr, "══════════════════════════════════════\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "WAYLAND_DISPLAY=%s\n", socket);
    fprintf(stderr, "Workspaces: %d\n", s->num_workspaces);
    fprintf(stderr, "\n");
    fprintf(stderr, "KEYS:\n");
    fprintf(stderr, "  Ctrl+Shift+Left/Right - Switch WS\n");
    fprintf(stderr, "  Ctrl+Shift+Z          - Command box\n");
    fprintf(stderr, "  Ctrl+Shift+X          - Cycle focus\n");
    fprintf(stderr, "  Ctrl+Shift+Down       - Exit\n");
    fprintf(stderr, "\n");
    
    s->running = true;
    wl_display_run(s->display);
    
    wl_display_destroy(s->display);
    fprintf(stderr, "\nElDinWM: Exit\n");
    return 0;
}
