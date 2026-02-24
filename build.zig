const std = @import("std");
const zgpu_build = @import("zgpu");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "vivid",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/runtime/main.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });

    // zglfw: compiles GLFW from source, provides Zig bindings
    const zglfw_dep = b.dependency("zglfw", .{});
    exe.root_module.addImport("zglfw", zglfw_dep.module("root"));
    exe.linkLibrary(zglfw_dep.artifact("glfw"));

    // zgpu: links pre-built Dawn, provides WebGPU bindings
    const zgpu_dep = b.dependency("zgpu", .{});
    exe.root_module.addImport("zgpu", zgpu_dep.module("root"));
    exe.linkLibrary(zgpu_dep.artifact("zdawn"));
    zgpu_build.addLibraryPathsTo(exe);
    zgpu_build.linkSystemDeps(b, exe);

    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const run_step = b.step("run", "Run Vivid");
    run_step.dependOn(&run_cmd.step);
}
