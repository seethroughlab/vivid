const std = @import("std");
const zglfw = @import("zglfw");
const zgpu = @import("zgpu");
const GpuContext = @import("gpu_context.zig").GpuContext;

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    zglfw.init() catch {
        std.log.err("Failed to initialize GLFW.", .{});
        return error.GlfwInitFailed;
    };
    defer zglfw.terminate();

    const window = zglfw.Window.create(1280, 800, "Vivid", null, null) catch {
        std.log.err("Failed to create GLFW window.", .{});
        return error.WindowCreateFailed;
    };
    defer window.destroy();

    var gpu = try GpuContext.init(allocator, window);
    defer gpu.deinit();

    // #16191D -> RGB normalized
    const clear_color = zgpu.wgpu.Color{
        .r = 0.086,
        .g = 0.098,
        .b = 0.114,
        .a = 1.0,
    };

    while (!window.shouldClose()) {
        zglfw.pollEvents();

        if (gpu.beginFrame()) |frame| {
            const pass = zgpu.beginRenderPassSimple(
                frame.encoder,
                .clear,
                frame.back_buffer_view,
                clear_color,
                null,
                null,
            );
            zgpu.endReleasePass(pass);

            gpu.endFrame(frame);
        }
    }
}
