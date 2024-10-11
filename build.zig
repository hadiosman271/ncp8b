const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "ncp8b",
        .root_source_file = b.path("ncp8b.zig"),
        .target = target,
        .optimize = optimize,
        .strip = true,
    });
    b.installArtifact(exe);


    exe.addIncludePath(b.path("extern/include"));
    const libs = [_][]const u8{
        "ncursesw", // ncurses: c
        "avformat", "avcodec", "avutil", "swscale" // ffmpeg: c m z pthread drm
    };
    inline for (libs) |lib| {
        exe.addObjectFile(b.path("extern/lib/lib"++lib++".a")); 
    }
    const ma_prefix = "extern/include/miniaudio/";
    exe.addIncludePath(b.path(ma_prefix));
    exe.addCSourceFile(.{.file = b.path(ma_prefix++"miniaudio.c"), .flags = &.{"-fno-sanitize=undefined"}});

    const system_libs = [_][]const u8{"c", "m", "z", "pthread", "drm"};
    inline for (system_libs) |lib| {
        exe.linkSystemLibrary(lib);
    }

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }
    const run_step = b.step("run", "Run");
    run_step.dependOn(&run_cmd.step);
}
