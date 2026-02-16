/*
 * ElDinWM - Minimalist Wayland Tiling Compositor
 * Pure C, wlroots 0.18 compatible
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

struct server {
    struct wl_display *wl_display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_seat *seat;
    struct wlr_output_layout *output_layout;
    struct wlr_xdg_shell *xdg_shell;
    
    struct wl_listener new_output;
    struct wl_listener new_xdg_surface;
    struct wl_listener new_input;
    
    struct wl_list keyboards;
    struct wl_list outputs;
    
    bool running;
};

struct keyboard {
    struct server *server;
    struct wlr_keyboard *wlr_keyboard;
    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_list link;
};

struct output {
    struct server *server;
    struct wlr_output *wlr_output;
    struct wl_listener frame;
    struct wl_list link;
};

static struct server g_server = {0};

static void handle_keybinding(struct server *server, xkb_keysym_t sym, uint32_t mods) {
    if ((mods & (WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT)) == (WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT)) {
        if (sym == XKB_KEY_Down) {
            wl_display_terminate(server->wl_display);
            server->running = false;
            fprintf(stderr, "ElDinWM: Exiting via Ctrl+Shift+Down\n");
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
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
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

static void server_new_input(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;
    
    if (device->type == WLR_INPUT_DEVICE_KEYBOARD) {
        server_new_keyboard(server, device);
    }
    
    wlr_seat_set_capabilities(server->seat, WL_SEAT_CAPABILITY_KEYBOARD);
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    fprintf(stderr, "ElDinWM: Window mapped\n");
}

static void server_new_xdg_surface(struct wl_listener *listener, void *data) {
    struct server *server = wl_container_of(listener, server, new_xdg_surface);
    struct wlr_xdg_surface *xdg_surface = data;
    
    if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        return;
    }
    
    wlr_scene_xdg_surface_create(&server->scene->tree, xdg_surface);
    
    struct wl_listener *map_listener = calloc(1, sizeof(struct wl_listener));
    map_listener->notify = xdg_toplevel_map;
    wl_signal_add(&xdg_surface->surface->events.map, map_listener);
    
    fprintf(stderr, "ElDinWM: New XDG surface created\n");
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
    
    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    
    wlr_output_layout_add_auto(server->output_layout, wlr_output);
    wlr_scene_output_create(server->scene, wlr_output);
    
    wl_list_insert(&server->outputs, &output->link);
    
    fprintf(stderr, "ElDinWM: Output added\n");
}

static void handle_signal(int sig) {
    wl_display_terminate(g_server.wl_display);
    g_server.running = false;
}

int main(void) {
    wlr_log_init(WLR_ERROR, NULL);
    
    struct server *server = &g_server;
    wl_list_init(&server->keyboards);
    wl_list_init(&server->outputs);
    
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGCHLD, SIG_IGN);
    
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
    
    server->seat = wlr_seat_create(server->wl_display, "seat0");
    
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
    fprintf(stderr, "ElDinWM: Press Ctrl+Shift+Down to exit\n");
    
    server->running = true;
    wl_display_run(server->wl_display);
    
    wl_display_destroy(server->wl_display);
    fprintf(stderr, "ElDinWM: Exited cleanly\n");
    return 0;
}
