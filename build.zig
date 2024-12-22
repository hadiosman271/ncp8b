const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "ncp8b",
        .root_source_file = b.path("src/ncp8b.zig"),
        .target = target,
        .optimize = optimize,
    });
    b.installArtifact(exe);
    exe.addCSourceFile(.{.file = b.path("src/c/log.c")});


    //// miniaudio
    //exe.addIncludePath(b.path("extern/miniaudio-0.11.21"));
    //exe.addObjectFile(b.path("extern/miniaudio-0.11.21/miniaudio.o"));

    // ffmpeg
    const libav_libs = [_][]const u8{
        "avformat", "avcodec", "avutil", "swscale", // need to link: c m z pthread drm
    };
    exe.addIncludePath(b.path("extern/ffmpeg-4.2.2/include"));
    inline for (libav_libs) |lib| exe.addObjectFile(b.path("extern/ffmpeg-4.2.2/lib/lib"++lib++".a"));

    exe.linkSystemLibrary("z");
    exe.linkLibC();

    // ncurses
    exe.linkSystemLibrary("ncurses");


    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }
    const run_step = b.step("run", "Run");
    run_step.dependOn(&run_cmd.step);
}
