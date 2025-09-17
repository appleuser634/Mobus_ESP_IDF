const std = @import("std");

extern "env" fn host_clear_screen() void;
extern "env" fn host_present() void;
extern "env" fn host_fill_rect(x: i32, y: i32, w: i32, h: i32, color: i32) void;
extern "env" fn host_draw_text(x: i32, y: i32, ptr: [*]const u8, len: i32, invert: i32) void;
extern "env" fn host_draw_sprite(id: i32, frame: i32, x: i32, y: i32) void;
extern "env" fn host_get_input() u32;
extern "env" fn host_random(max: i32) i32;
extern "env" fn host_sleep(ms: i32) void;
extern "env" fn host_time_ms() u32;

const Input = struct {
    pub const action = 1 << 0;
    pub const enter = 1 << 1;
    pub const back = 1 << 2;
    pub const joy_left = 1 << 3;
    pub const joy_right = 1 << 4;
    pub const joy_up = 1 << 5;
    pub const joy_down = 1 << 6;
};

const Sprite = struct {
    pub const kuina = 0;
};

const Vec = struct {
    x: i32,
    y: i32,
};

const State = enum { intro, running };

var state: State = .intro;
var blink_ms: u32 = 0;
var anim_ms: u32 = 0;
var scroll_x: i32 = -160;
var frame: i32 = 0;
var start_time: u32 = 0;

const kuina_positions = [_]Vec{
    .{ .x = 0, .y = 0 },
    .{ .x = 10, .y = 50 },
    .{ .x = 20, .y = 10 },
    .{ .x = 30, .y = 30 },
    .{ .x = 40, .y = 0 },
    .{ .x = 50, .y = 50 },
    .{ .x = 60, .y = 20 },
    .{ .x = 70, .y = 40 },
    .{ .x = 80, .y = 10 },
    .{ .x = 90, .y = 50 },
    .{ .x = 100, .y = 0 },
    .{ .x = 110, .y = 50 },
    .{ .x = 120, .y = 10 },
    .{ .x = 130, .y = 30 },
    .{ .x = 140, .y = 0 },
    .{ .x = 150, .y = 50 },
    .{ .x = 160, .y = 20 },
};

fn draw_intro(show_prompt: bool) void {
    const title = "MOPPING";
    host_draw_text(38, 16, title.ptr, @as(i32, @intCast(title.len)), 0);
    if (show_prompt) {
        const start = "start";
        host_draw_text(50, 40, start.ptr, @as(i32, @intCast(start.len)), 1);
    } else {
        host_fill_rect(48, 38, 40, 16, 0);
        const start = "start";
        host_draw_text(50, 40, start.ptr, @as(i32, @intCast(start.len)), 0);
    }
}

fn draw_running(elapsed_ms: u32) void {
    const title = "MOPPING";
    host_draw_text(38, 4, title.ptr, @as(i32, @intCast(title.len)), 0);

    var i: usize = 0;
    while (i < kuina_positions.len) : (i += 1) {
        const pos = kuina_positions[i];
        const x = pos.x + scroll_x;
        if (x > -16 and x < 128) {
        host_draw_sprite(Sprite.kuina, frame, x, pos.y);
        }
    }

    if (scroll_x > 64) {
        const start = "start";
        const invert = @as(i32, @intCast((elapsed_ms / 300) % 2));
        host_draw_text(50, 40, start.ptr, @as(i32, @intCast(start.len)), invert);
    }
}

pub export fn game_init() void {
    state = .intro;
    blink_ms = 0;
    anim_ms = 0;
    scroll_x = -160;
    frame = 0;
    start_time = host_time_ms();
    host_clear_screen();
    host_present();
}

pub export fn game_update(dt_ms: u32) u32 {
    blink_ms += dt_ms;
    anim_ms += dt_ms;

    const input = host_get_input();

    host_clear_screen();

    switch (state) {
        .intro => {
            const show_prompt = (blink_ms / 300) % 2 == 0;
            draw_intro(show_prompt);
            if ((input & (Input.action | Input.enter)) != 0) {
                state = .running;
                blink_ms = 0;
                anim_ms = 0;
                scroll_x = -160;
                frame = 0;
            }
            if ((input & Input.back) != 0) {
                host_present();
                return 1;
            }
        },
        .running => {
            if (anim_ms >= 120) {
                anim_ms = 0;
                frame = 1 - frame;
            }
            if (scroll_x < 180) {
                scroll_x += 1;
            }
            const elapsed = host_time_ms() - start_time;
            draw_running(elapsed);
            if ((input & (Input.back | Input.joy_left)) != 0) {
                host_present();
                return 1;
            }
        },
    }

    host_present();
    host_sleep(16);
    return 0;
}

pub export fn _start() void {}
