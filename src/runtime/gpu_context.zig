const std = @import("std");
const zgpu = @import("zgpu");
const zglfw = @import("zglfw");

pub const GpuContext = struct {
    gctx: *zgpu.GraphicsContext,
    allocator: std.mem.Allocator,

    pub fn init(allocator: std.mem.Allocator, window: *zglfw.Window) !GpuContext {
        const gctx = try zgpu.GraphicsContext.create(allocator, .{
            .window = @ptrCast(window),
            .fn_getTime = @ptrCast(&zglfw.getTime),
            .fn_getFramebufferSize = @ptrCast(&zglfw.Window.getFramebufferSize),
            .fn_getCocoaWindow = @ptrCast(&zglfw.getCocoaWindow),
        }, .{});

        return .{
            .gctx = gctx,
            .allocator = allocator,
        };
    }

    pub const FrameState = struct {
        back_buffer_view: zgpu.wgpu.TextureView,
        encoder: zgpu.wgpu.CommandEncoder,
    };

    pub fn beginFrame(self: *GpuContext) ?FrameState {
        if (!self.gctx.canRender()) return null;

        const back_buffer_view = self.gctx.swapchain.getCurrentTextureView();
        const encoder = self.gctx.device.createCommandEncoder(null);

        return .{
            .back_buffer_view = back_buffer_view,
            .encoder = encoder,
        };
    }

    pub fn endFrame(self: *GpuContext, frame: FrameState) void {
        const commands = frame.encoder.finish(null);
        defer commands.release();

        self.gctx.submit(&.{commands});

        frame.back_buffer_view.release();
        frame.encoder.release();

        _ = self.gctx.present();
    }

    pub fn deinit(self: *GpuContext) void {
        self.gctx.destroy(self.allocator);
    }
};
