// Notepatra palette preview - synthetic; no real data
// Exercises: const, var, fn, struct, enum, comptime, error union, defer,
// errdefer, @builtin functions, control flow.

const std = @import("std");

const PI: f64 = 3.14159;
const MAX_RETRIES: u32 = 0x10;

const Status = enum { pending, active, archived };

const Shape = union(enum) {
    circle: f64,
    square: f64,
    none,

    fn area(self: Shape) f64 {
        return switch (self) {
            .circle => |r| PI * r * r,
            .square => |s| s * s,
            .none => 0.0,
        };
    }
};

const User = struct {
    id: u32,
    name: []const u8,
    email: []const u8,
    status: Status = .pending,

    fn greet(self: User, writer: anytype) !void {
        try writer.print("hello {s} <{s}>\n", .{ self.name, self.email });
    }
};

const VaultError = error{ Empty, Overflow };

fn classify(n: i32) []const u8 {
    if (n < 0) return "negative";
    if (n == 0) return "zero";
    return "positive";
}

fn sumAtLeastOne(values: []const i32) VaultError!i32 {
    if (values.len == 0) return VaultError.Empty;
    var total: i32 = 0;
    for (values) |v| {
        total = std.math.add(i32, total, v) catch return VaultError.Overflow;
    }
    return total;
}

pub fn main() !void {
    const stdout = std.io.getStdOut().writer();
    defer stdout.print("done\n", .{}) catch {};
    errdefer stdout.print("errdefer fired\n", .{}) catch {};

    const users = [_]User{
        .{ .id = 1, .name = "Alice", .email = "alice@example.com", .status = .active },
        .{ .id = 2, .name = "Bob", .email = "bob@example.org" },
    };
    const shapes = [_]Shape{ .{ .circle = 1.5 }, .{ .square = 2.0 }, .none };

    inline for (.{ PI, MAX_RETRIES }) |c| {
        try stdout.print("const={any}\n", .{c});
    }
    for (users) |u| try u.greet(stdout);
    for (shapes) |s| try stdout.print("area={d:.2}\n", .{s.area()});

    const total = try sumAtLeastOne(&[_]i32{ 1, 2, 3 });
    try stdout.print("total={d} kind={s} bits={d}\n", .{ total, classify(-3), @sizeOf(u32) });
}
