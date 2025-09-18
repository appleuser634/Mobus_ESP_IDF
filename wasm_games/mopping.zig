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

const Sprite = enum(u8) {
    kuina = 0,
    mongoose = 1,
    grass = 2,
    pineapple = 3,
    mongoose_left = 4,
    earthworm = 5,
    hart = 6,
};

const Character = struct {
    sprite: Sprite,
    x: i32,
    y: i32,
    width: i32,
    height: i32,
    visible: bool,
    frame_count: u8,

    fn draw(self: *const Character, frame_toggle: bool) void {
        if (!self.visible) return;
        const sprite_id = @as(i32, @intCast(@intFromEnum(self.sprite)));
        const frame: i32 = if (self.frame_count > 1 and frame_toggle) 1 else 0;
        host_draw_sprite(sprite_id, frame, self.x, self.y);
    }
};

const Enemy = struct {
    id: u8,
    character: Character,
};

fn makeCharacter(sprite: Sprite, x: i32, y: i32, width: i32, height: i32, frame_count: u8) Character {
    return Character{
        .sprite = sprite,
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .visible = true,
        .frame_count = frame_count,
    };
}

fn randomRange(max: i32) i32 {
    if (max <= 0) return 0;
    const r = host_random(max);
    if (r < 0) return 0;
    return r;
}

const State = enum {
    OpeningScroll,
    OpeningWait,
    OpeningParade,
    OpeningMongoose,
    Running,
    GameOverFlash,
    GameOverWait,
};

var state: State = .OpeningScroll;

const parade_length = 17;
const kuina_start_x = 15;
const kuina_start_y = 45;
const max_jump = 30;
const hart_duration = 60;

var opening_scroll_pos: i32 = 0;
var opening_scroll_timer: u32 = 0;
var opening_blink_timer: u32 = 0;

var opening_parade_started = false;
var opening_parade: [parade_length]Character = undefined;
var opening_parade_anim_timer: u32 = 0;
var opening_parade_move_timer: u32 = 0;
var opening_parade_frame_toggle = false;

var opening_mongoose_started = false;
var opening_mongoose_anim_timer: u32 = 0;
var opening_mongoose_move_timer: u32 = 0;
var opening_mongoose_frame_toggle = false;
var opening_mongoose_char: Character = undefined;

var running_initialized = false;
var kuina_char: Character = undefined;
var enemies: [3]Enemy = undefined;
var flip_counter: i32 = 0;
var flip_frame_toggle = false;
var jump_active = false;
var jump_progress: i32 = 0;
var score: i32 = 0;
var frame_counter: i32 = 0;
var hart_active = false;
var hart_timer: i32 = 0;
var final_score: i32 = 0;
var game_over_timer: u32 = 0;

fn reset_all() void {
    state = .OpeningScroll;
    opening_scroll_pos = 0;
    opening_scroll_timer = 0;
    opening_blink_timer = 0;
    opening_parade_started = false;
    opening_parade_anim_timer = 0;
    opening_parade_move_timer = 0;
    opening_parade_frame_toggle = false;
    opening_mongoose_started = false;
    opening_mongoose_anim_timer = 0;
    opening_mongoose_move_timer = 0;
    opening_mongoose_frame_toggle = false;
    running_initialized = false;
    flip_counter = 0;
    flip_frame_toggle = false;
    jump_active = false;
    jump_progress = 0;
    score = 0;
    frame_counter = 0;
    hart_active = false;
    hart_timer = 0;
    final_score = 0;
    game_over_timer = 0;
}

fn start_parade() void {
    opening_parade_started = true;
    opening_parade_anim_timer = 0;
    opening_parade_move_timer = 0;
    opening_parade_frame_toggle = false;

    const base_x: i32 = -130;
    const offsets = [_]struct { dx: i32, dy: i32 }{
        .{ .dx = 0, .dy = 0 },
        .{ .dx = 10, .dy = 50 },
        .{ .dx = 20, .dy = 10 },
        .{ .dx = 30, .dy = 30 },
        .{ .dx = 40, .dy = 0 },
        .{ .dx = 50, .dy = 50 },
        .{ .dx = 60, .dy = 20 },
        .{ .dx = 70, .dy = 40 },
        .{ .dx = 80, .dy = 10 },
        .{ .dx = 90, .dy = 50 },
        .{ .dx = 100, .dy = 0 },
        .{ .dx = 110, .dy = 50 },
        .{ .dx = 120, .dy = 10 },
        .{ .dx = 130, .dy = 30 },
        .{ .dx = 140, .dy = 0 },
        .{ .dx = 150, .dy = 50 },
        .{ .dx = 160, .dy = 20 },
    };

    var i: usize = 0;
    while (i < parade_length) : (i += 1) {
        const off = offsets[i];
        opening_parade[i] = makeCharacter(
            Sprite.kuina,
            base_x + off.dx,
            off.dy,
            16,
            16,
            2,
        );
    }
}

fn start_mongoose() void {
    opening_mongoose_started = true;
    opening_mongoose_anim_timer = 0;
    opening_mongoose_move_timer = 0;
    opening_mongoose_frame_toggle = false;
    opening_mongoose_char = makeCharacter(Sprite.mongoose, 0, 30, 8, 8, 2);
}

fn set_enemy(enemy: *Enemy, id: u8, x: i32) void {
    enemy.id = id;
    const character = switch (id) {
        0 => makeCharacter(Sprite.grass, x, 53, 8, 8, 1),
        1 => makeCharacter(Sprite.pineapple, x, 45, 16, 16, 1),
        2 => makeCharacter(Sprite.mongoose_left, x, 53, 16, 8, 2),
        else => makeCharacter(Sprite.earthworm, x, 53, 8, 8, 2),
    };
    enemy.character = character;
}

fn next_enemy_id() u8 {
    const r = randomRange(10);
    if (r <= 5) return 0; // grass
    if (r <= 8) return 1; // pineapple
    if (r <= 9) return 2; // mongoose left
    return 3;             // earthworm (rare / unreachable in original logic)
}

fn start_running_game() void {
    running_initialized = true;
    flip_counter = 0;
    flip_frame_toggle = false;
    jump_active = false;
    jump_progress = 0;
    score = 0;
    frame_counter = 0;
    hart_active = false;
    hart_timer = 0;

    kuina_char = makeCharacter(Sprite.kuina, kuina_start_x, kuina_start_y, 16, 16, 2);

    set_enemy(&enemies[0], 0, 130);
    set_enemy(&enemies[1], 0, 160);
    set_enemy(&enemies[2], 1, 210);
}

fn draw_title(y: i32) void {
    const title = "MOPPING";
    host_draw_text(38, y, title.ptr, @as(i32, @intCast(title.len)), 0);
}

fn draw_start_prompt(invert: bool) void {
    // cover the prompt area to avoid ghosting when toggling
    host_fill_rect(45, 38, 40, 18, 0);
    const start = "start";
    const inv: i32 = if (invert) 1 else 0;
    host_draw_text(50, 40, start.ptr, @as(i32, @intCast(start.len)), inv);
}

fn draw_score(current_score: i32, y: i32) void {
    var buf: [32]u8 = undefined;
    const remaining = std.fmt.bufPrint(buf[0..], "Score:{d}", .{current_score}) catch buf[0..0];
    const used_len = @as(usize, buf.len) - remaining.len;
    const slice = buf[0..used_len];
    const ptr: [*]const u8 = slice.ptr;
    host_draw_text(0, y, ptr, @as(i32, @intCast(used_len)), 0);
}

fn handle_enemy_recycle() void {
    if (enemies[0].character.x < 0) {
        enemies[0] = enemies[1];
        enemies[1] = enemies[2];
        const next_x = enemies[1].character.x + 60 + randomRange(30);
        const id = next_enemy_id();
        set_enemy(&enemies[2], id, next_x);
    }
}

fn check_collision(enemy: *const Enemy) bool {
    if (!enemy.character.visible) return false;
    const enemy_center = enemy.character.x + 12;
    if (kuina_char.x + 12 >= enemy_center) return false;
    if (kuina_char.x + 4 + kuina_char.width <= enemy_center) return false;
    const threshold = kuina_start_y - enemy.character.height;
    return kuina_char.y > threshold;
}

fn draw_hart() void {
    host_draw_sprite(@as(i32, @intCast(@intFromEnum(Sprite.hart))), 0, 30, 30);
}

fn show_game_over_message() void {
    const msg = "GAME OVER!!";
    host_draw_text(25, 20, msg.ptr, @as(i32, @intCast(msg.len)), 0);
}

fn show_retry_screen() void {
    draw_score(final_score, 20);
    const prompt = "Type=Retry";
    host_draw_text(30, 36, prompt.ptr, @as(i32, @intCast(prompt.len)), 0);
    const exit_msg = "Back=Exit";
    host_draw_text(34, 48, exit_msg.ptr, @as(i32, @intCast(exit_msg.len)), 0);
}

pub export fn game_init() void {
    reset_all();
    host_clear_screen();
    host_present();
}

pub export fn game_update(dt_ms: u32) u32 {
    const input = host_get_input();

    host_clear_screen();

    var exit_requested = false;

    switch (state) {
        .OpeningScroll => {
            opening_scroll_timer += dt_ms;
            if (opening_scroll_timer >= 50) {
                opening_scroll_timer -= 50;
                if (opening_scroll_pos < 20) {
                    opening_scroll_pos += 1;
                } else {
                    state = .OpeningWait;
                    opening_blink_timer = 0;
                }
            }
            draw_title(opening_scroll_pos);
        },
        .OpeningWait => {
            if ((input & (Input.back | Input.joy_left)) != 0) {
                exit_requested = true;
            } else {
                opening_blink_timer += dt_ms;
                const show_prompt = ((opening_blink_timer / 300) % 2) == 0;
                draw_title(20);
                draw_start_prompt(!show_prompt);
                if ((input & (Input.action | Input.enter)) != 0) {
                    state = .OpeningParade;
                    start_parade();
                }
            }
        },
        .OpeningParade => {
            if (!opening_parade_started) {
                start_parade();
            }

            opening_parade_anim_timer += dt_ms;
            if (opening_parade_anim_timer >= 120) {
                opening_parade_anim_timer -= 120;
                opening_parade_frame_toggle = !opening_parade_frame_toggle;
            }

            opening_parade_move_timer += dt_ms;
            if (opening_parade_move_timer >= 16) {
                opening_parade_move_timer -= 16;
                var i: usize = 0;
                while (i < parade_length) : (i += 1) {
                    opening_parade[i].x += 1;
                }
            }

            draw_title(20);
            draw_start_prompt(false);

            var i: usize = 0;
            while (i < parade_length) : (i += 1) {
                if (opening_parade[i].x > -16) {
                    opening_parade[i].draw(opening_parade_frame_toggle);
                }
            }

            if (opening_parade[0].x > 128) {
                state = .OpeningMongoose;
                start_mongoose();
            }
        },
        .OpeningMongoose => {
            if (!opening_mongoose_started) {
                start_mongoose();
            }

            opening_mongoose_anim_timer += dt_ms;
            if (opening_mongoose_anim_timer >= 120) {
                opening_mongoose_anim_timer -= 120;
                opening_mongoose_frame_toggle = !opening_mongoose_frame_toggle;
            }

            opening_mongoose_move_timer += dt_ms;
            if (opening_mongoose_move_timer >= 16) {
                opening_mongoose_move_timer -= 16;
                opening_mongoose_char.x += 1;
            }

            draw_title(20);
            draw_start_prompt(false);
            opening_mongoose_char.draw(opening_mongoose_frame_toggle);

            if (opening_mongoose_char.x > 128) {
                state = .Running;
                start_running_game();
            }
        },
        .Running => {
            if ((input & (Input.back | Input.joy_left)) != 0) {
                exit_requested = true;
            } else {
                if (!running_initialized) {
                    start_running_game();
                }

                if ((input & Input.action) != 0 and !jump_active) {
                    jump_active = true;
                    jump_progress = 0;
                }

                if (jump_active) {
                    if (jump_progress >= max_jump * 2) {
                        jump_active = false;
                        jump_progress = 0;
                        kuina_char.y = kuina_start_y;
                    } else if (jump_progress < max_jump) {
                        kuina_char.y -= 1;
                        jump_progress += 1;
                    } else {
                        kuina_char.y += 1;
                        jump_progress += 1;
                    }
                }

                flip_counter += 1;
                if (flip_counter > 20) {
                    flip_counter = 0;
                    flip_frame_toggle = !flip_frame_toggle;
                }

                var e_idx: usize = 0;
                while (e_idx < enemies.len) : (e_idx += 1) {
                    if (enemies[e_idx].character.visible) {
                        enemies[e_idx].character.draw(flip_frame_toggle);
                        enemies[e_idx].character.x -= 1;
                    }
                }

                handle_enemy_recycle();

                e_idx = 0;
                while (e_idx < enemies.len) : (e_idx += 1) {
                    const enemy = &enemies[e_idx];
                    if (!enemy.character.visible) continue;
                    if (!check_collision(enemy)) continue;

                    if (enemy.id == 1 or enemy.id == 2) {
                        state = .GameOverFlash;
                        final_score = score;
                        game_over_timer = 0;
                        jump_active = false;
                        hart_active = false;
                        break;
                    } else if (enemy.id == 3) {
                        score += 100;
                        enemy.character.visible = false;
                        hart_active = true;
                        hart_timer = 0;
                    }
                }

                if (state == .GameOverFlash) {
                    show_game_over_message();
                } else {
                    if (hart_active) {
                        if (hart_timer < hart_duration) {
                            draw_hart();
                            hart_timer += 1;
                        } else {
                            hart_active = false;
                            hart_timer = 0;
                        }
                    }

                    kuina_char.draw(flip_frame_toggle);

                    frame_counter += 1;
                    if (frame_counter > 10) {
                        score += 1;
                        frame_counter = 0;
                    }

                    draw_score(score, 0);
                }
            }
        },
        .GameOverFlash => {
            game_over_timer += dt_ms;
            show_game_over_message();
            if (game_over_timer >= 1000) {
                state = .GameOverWait;
            }
        },
        .GameOverWait => {
            show_retry_screen();
            if ((input & Input.action) != 0) {
                state = .Running;
                start_running_game();
            } else if ((input & (Input.back | Input.joy_left | Input.enter)) != 0) {
                exit_requested = true;
            }
        },
    }

    if (exit_requested) {
        host_present();
        host_sleep(16);
        return 1;
    }

    host_present();
    host_sleep(16);
    return 0;
}

pub export fn _start() void {}
