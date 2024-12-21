// TODO: zig-ify

const std = @import("std");

const c = @cImport({
    @cInclude("miniaudio.h");
    @cInclude("ncursesw/ncurses.h");

    @cInclude("libavformat/avformat.h");
    @cInclude("libavcodec/avcodec.h");
    @cInclude("libavutil/avutil.h");
    @cInclude("libavutil/cpu.h");
    @cInclude("libavutil/imgutils.h");
    @cInclude("libswscale/swscale.h");

    @cInclude("time.h");
    @cInclude("stdio.h");
});

pub fn main() !void {
    //var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    //defer _ = gpa.deinit();
    //const alloc = gpa_config.allocator();

    //const args = try std.process.argsAlloc(alloc);
    //defer std.process.argsFree(alloc, args);

    //const stdin = std.io.getStdIn().reader();
    //const stdout = std.io.getStdOut().writer();

    // miniaudio
    var device_config = c.ma_device_config_init(c.ma_device_type_playback);
    device_config.dataCallback = data_callback;

    var device: c.ma_device = undefined;
    if (c.ma_device_init(null, &device_config, &device) != c.MA_SUCCESS) {
        return error.ma_device_init;
    }
    defer c.ma_device_uninit(&device);

    std.debug.print("device info: \n", .{});
    std.debug.print("      type: {d} \n", .{device.type});
    std.debug.print("    format: {d} \n", .{device.playback.format});
    std.debug.print("  channels: {d} \n", .{device.playback.channels});

    // ffmpeg
    c.av_log_set_level(c.AV_LOG_QUIET);
    var media = try Media.open("video.mp4");
    defer media.close();

    //// ncurses
    //const aspect_ratio = @as(f32, @floatFromInt(media.video.s.?.*.codecpar.*.width)) / @as(f32, @floatFromInt(media.video.s.?.*.codecpar.*.height));
    //var player = Player.init(aspect_ratio);
    //defer player.deinit();

    //media.set_video_size(player.width, player.height);
    media.print_info();
    //media.print_info(player.tab[1]);
    _ = c.fflush(c.stdout);

    //// main loop
    //var ch = c.getch();
    //while (ch != 'q') : (ch = c.getch()) {
    //    media.decode_frame() catch break;
    //    player.current_video_frame = player.current_video_frame orelse media.video.queue.peek();
    //    player.current_audio_frame = player.current_audio_frame orelse media.audio.queue.peek();

    //    if (player.current_video_frame == null and player.current_audio_frame == null) {
    //        break;
    //    }

    //    if (player.current_video_frame) |video| {
    //        if (media.video.dt >= video.*.pts) {
    //            player.draw();
    //        media.video.queue.pop();
    //        player.current_video_frame = null;
    //        }
    //    }
    //    print_frame_info(player, media, player.current_video_frame, player.current_audio_frame);
    //}
}

const Player = struct {
    width: usize,
    height: usize,
    tab: [3]*c.WINDOW,

    current_video_frame: ?*c.AVFrame,
    current_audio_frame: ?*c.AVFrame,

    const LOG_MAX_LINES = 10000;

    pub fn init(aspect_ratio: f32) Player {
        _ = c.initscr(); _ = c.cbreak(); _ = c.noecho(); _ = c.nonl();
        var player: Player = undefined;
        player.height = @intCast(c.LINES - 1);
        player.width = @intFromFloat(@as(f32, @floatFromInt(player.height)) * aspect_ratio);
        player.tab = .{
            c.newwin(c.LINES - 1, c.COLS, 1, 0).?,
            c.newwin(c.LINES - 1, c.COLS, 1, 0).?,
            c.newpad(LOG_MAX_LINES, c.COLS).?,
        };
        for (player.tab) |tab| {
            _ = c.nodelay(tab, true);
            _ = c.keypad(tab, true);
        }
        player.current_video_frame = null;
        player.current_audio_frame = null;
        return player;
    }

    pub fn deinit(self: Player) void {
        _ = self;
        _ = c.endwin();
    }

    pub fn draw(self: Player) void {
        const video = self.current_video_frame orelse return;

        const player_win = self.tab[0];

        const win_start = @as(usize, @intCast(@divTrunc(c.COLS, 2))) - self.width;
        const pixel = video.*.data[0];
        const linesize = video.*.linesize[0];

        _ = c.wmove(player_win, 0, @intCast(win_start));
        for (0..self.width * self.height) |i| {
            const idx = (i / self.width) * @as(usize, @intCast(linesize)) + (i % self.width) * 3;

            const brightness: usize = @intFromFloat((
                0.299 * @as(f32, @floatFromInt(pixel[idx]))     + // r
                0.587 * @as(f32, @floatFromInt(pixel[idx + 1])) + // g
                0.114 * @as(f32, @floatFromInt(pixel[idx + 2]))   // b
            ) / 256.0 * 13.0); // scale [0, 256) to [0, 13)

            //c.attrset(COLOR_PAIR(rgb2ansi(pixel)));
            const ch = " .,-~:;=!*#$@"[brightness];
            _ = c.waddch(player_win, ch);
            _ = c.waddch(player_win, ch);
            _ = c.attrset(c.A_NORMAL);

            if ((i + 1) % self.width == 0) {
                _ = c.wmove(player_win, c.getcury(player_win) + 1, @intCast(win_start));
            }
        }
    }
};

fn data_callback(device_ptr: ?*anyopaque, input: ?*anyopaque, output: ?*const anyopaque, frame_count: c.ma_uint32) callconv(.C) void {
    const device: *c.ma_device = @ptrCast(@alignCast(device_ptr));
    _ = device;
    _ = input;
    _ = output;
    _ = frame_count;
}


const Media = struct {
    url: []const u8,
    format_ctx: [*c]c.AVFormatContext,
    video: Track,
    audio: Track,

    const QUEUE_SIZE = 32;
    const FrameQueue = struct {
        frame: [QUEUE_SIZE][*c]c.AVFrame,
        start: usize, size: usize,

        pub fn peek(q: FrameQueue) ?*c.AVFrame {
            if (q.size > 0) {
                return q.frame[q.start];
            }
            else {
                return null;
            }
        }

        pub fn pop(q: *FrameQueue) void {
            q.start = (q.start + 1) % QUEUE_SIZE;
            q.size -= 1;
        }
    };

    const Track = struct {
        s: ?*c.AVStream,
        idx: isize,
        codec_ctx: *c.AVCodecContext,
        codec: *const c.AVCodec,
        queue: FrameQueue,
        t1: u64, dt: u64,

        pub fn open_codec(t: *Track) void {
            t.codec = c.avcodec_find_decoder(t.s.?.*.codecpar.*.codec_id);
            t.codec_ctx = c.avcodec_alloc_context3(t.codec);
            _ = c.avcodec_parameters_to_context(t.codec_ctx, t.s.?.*.codecpar);
            _ = c.avcodec_open2(t.codec_ctx, t.codec, null);
        }
    };


    var av_packet: ?*c.AVPacket = null;
    var av_frame: ?*c.AVFrame = null;
    var sws_ctx: ?*c.SwsContext = null;

    pub fn open(url: []const u8) !Media {
        var m: Media = undefined;
        m.format_ctx = null;
        m.url = url;
        if (c.avformat_open_input(&m.format_ctx, @as([*c]const u8, @ptrCast(m.url)), null, null) < 0) {
            std.debug.print("error: failed to open {s}\n", .{m.url});
            return error.open;
        }

        _ = c.avformat_find_stream_info(m.format_ctx, null);
        m.video.s = null;
        m.audio.s = null;

        for (0..m.format_ctx.*.nb_streams) |i| {
            const stream: *c.AVStream = m.format_ctx.*.streams[i];

            if (m.video.s == null and stream.codecpar.*.codec_type == c.AVMEDIA_TYPE_VIDEO) {
                m.video.s = stream;
                m.video.idx = @as(isize, @intCast(i));
                m.video.open_codec();
            } else if (m.audio.s == null and stream.codecpar.*.codec_type == c.AVMEDIA_TYPE_AUDIO) {
                m.audio.s = stream;
                m.audio.idx = @as(isize, @intCast(i));
                m.audio.open_codec();
            }
        }

        av_frame = c.av_frame_alloc();
        av_packet = c.av_packet_alloc();
        for (0..QUEUE_SIZE) |i| {
            m.video.queue.frame[i] = c.av_frame_alloc();
            _ = c.av_image_alloc(&m.video.queue.frame[i].*.data, &m.video.queue.frame[i].*.linesize,
                m.video.s.?.*.codecpar.*.width, m.video.s.?.*.codecpar.*.height,
                c.AV_PIX_FMT_RGB24, @as(c_int, @intCast(c.av_cpu_max_align()))
            );
            m.audio.queue.frame[i] = c.av_frame_alloc();
        }
        sws_ctx = c.sws_getContext(
            m.video.s.?.*.codecpar.*.width, m.video.s.?.*.codecpar.*.height,
            m.video.s.?.*.codecpar.*.format,
            m.video.s.?.*.codecpar.*.width, m.video.s.?.*.codecpar.*.height,
            c.AV_PIX_FMT_RGB24, 0, null, null, null
        );

        const t: c.clock_t = c.clock();
        m.video.t1 = @intCast(@divTrunc(t * m.video.s.?.*.time_base.den, c.CLOCKS_PER_SEC));
        m.audio.t1 = @intCast(@divTrunc(t * m.audio.s.?.*.time_base.den, c.CLOCKS_PER_SEC));

        return m;
    }

    pub fn close(m: *Media) void {
        for (0..QUEUE_SIZE) |i| {
            c.av_freep(@ptrCast(&m.video.queue.frame[i].*.data[0]));
            c.av_frame_free(&m.video.queue.frame[i]);
        }
        for (0..QUEUE_SIZE) |i| {
            c.av_frame_free(&m.audio.queue.frame[i]);
        }

        c.av_packet_free(&av_packet);
        c.av_frame_free(&av_frame);
        c.sws_freeContext(sws_ctx);

        c.avcodec_free_context(@ptrCast(&m.video.codec_ctx));
        c.avcodec_free_context(@ptrCast(&m.audio.codec_ctx));
        c.avformat_close_input(&m.format_ctx);

        m.* = undefined;
    }

    pub fn set_video_size(m: *Media, width: usize, height: usize) void {
        for (0..QUEUE_SIZE) |i| {
            c.av_freep(@ptrCast(&m.video.queue.frame[i].*.data[0]));
            _ = c.av_image_alloc(&m.video.queue.frame[i].*.data, &m.video.queue.frame[i].*.linesize,
                @intCast(width), @intCast(height), c.AV_PIX_FMT_RGB24, @intCast(c.av_cpu_max_align())
            );
        }
        c.sws_freeContext(sws_ctx);
        sws_ctx = c.sws_getContext(
            m.video.s.?.*.codecpar.*.width, m.video.s.?.*.codecpar.*.height,
            m.video.s.?.*.codecpar.*.format,
            @intCast(width), @intCast(height), c.AV_PIX_FMT_RGB24, 0, null, null, null
        );
    }

    //pub fn print_info(m: Media, win: *c.WINDOW) void {
    pub fn print_info(m: Media) void {
        //_ = c.wprintw(win,
        _ = c.printf(
            "%s:\nformat: %s, duration: %ld\n",
            m.url.ptr, m.format_ctx.*.iformat.*.long_name, m.format_ctx.*.duration
        );
        //_ = c.wprintw(win,
        _ = c.printf(
            "video (%d):\n" ++
            "  resolution: %dx%d, frame rate: %d/%d, time base: %d/%d\n" ++
            "  codec: %s, bit rate: %ld\n",
            m.video.idx, m.video.s.?.*.codecpar.*.width, m.video.s.?.*.codecpar.*.height,
            m.video.s.?.*.avg_frame_rate.num, m.video.s.?.*.avg_frame_rate.den,
            m.video.s.?.*.time_base.num, m.video.s.?.*.time_base.den, m.video.codec.*.long_name, m.video.s.?.*.codecpar.*.bit_rate
        );

        //_ = c.wprintw(win,
        _ = c.printf(
            "audio (%d):\n" ++
            "  channels: %d, sample rate: %d, time base: %d/%d\n" ++
            "  codec: %s, bit rate: %ld\n",
            m.audio.idx, m.audio.s.?.*.codecpar.*.channels, m.audio.s.?.*.codecpar.*.sample_rate,
            m.audio.s.?.*.time_base.num, m.audio.s.?.*.time_base.den,
            m.audio.codec.*.long_name, m.audio.s.?.*.codecpar.*.bit_rate
        );
    }

    pub fn decode_frame(m: *Media) !void {
        if (m.video.queue.size < QUEUE_SIZE and m.audio.queue.size < QUEUE_SIZE) {
            if (c.av_read_frame(m.format_ctx, av_packet) < 0)
                return error.av_read_frame;
            if (av_packet.?.*.stream_index == m.video.idx) {
                _ = c.avcodec_send_packet(m.video.codec_ctx, av_packet);

                const response = c.avcodec_receive_frame(m.video.codec_ctx, av_frame);
                if (response != c.AVERROR(c.EAGAIN) and response != c.AVERROR_EOF) {
                    const v_frame: *c.AVFrame = m.video.queue.frame[(m.video.queue.start + m.video.queue.size) % QUEUE_SIZE];

                    _ = c.sws_scale(sws_ctx,
                        &av_frame.?.*.data, &av_frame.?.*.linesize,
                        0, m.video.s.?.*.codecpar.*.height, &v_frame.*.data, &v_frame.*.linesize
                    );

                    v_frame.*.key_frame = m.video.codec_ctx.*.frame_number; // using key_frame to store frame number
                    v_frame.*.pts = av_frame.?.*.pts;
                    m.video.queue.size += 1;
                }
            } else if (av_packet.?.*.stream_index == m.audio.idx) {
                _ = c.avcodec_send_packet(m.audio.codec_ctx, av_packet);

                const a_frame: *c.AVFrame = m.audio.queue.frame[(m.audio.queue.start + m.audio.queue.size) % QUEUE_SIZE];

                const response = c.avcodec_receive_frame(m.audio.codec_ctx, a_frame);
                if (response != c.AVERROR(c.EAGAIN) and response != c.AVERROR_EOF) {
                    a_frame.*.key_frame = m.audio.codec_ctx.*.frame_number; // using key_frame to store frame number
                    m.audio.queue.size += 1;
                }
            }
            c.av_packet_unref(av_packet);
        }

        const t: c.clock_t = c.clock();
        m.video.dt = @as(u64, @intCast(@divTrunc(t * m.video.s.?.*.time_base.den, c.CLOCKS_PER_SEC))) - m.video.t1;
        m.audio.dt = @as(u64, @intCast(@divTrunc(t * m.audio.s.?.*.time_base.den, c.CLOCKS_PER_SEC))) - m.audio.t1;
    }
};

pub fn print_frame_info(player: Player, m: Media, video: ?*c.AVFrame, audio: ?*c.AVFrame) void {
    const info: *c.WINDOW = player.tab[0];
    if (video) |v| {
        _ = c.wmove(info, 12, 0);
        _ = c.wprintw(info,
            "video:\n" ++
            "  frame: %4d/%4d: pts: %6ld dt: %6ld\n" ++
            "  queue: start: %2ld size: %2ld\n",
            v.*.key_frame, m.video.codec_ctx.*.frame_number, v.*.pts, m.video.dt,
            m.video.queue.start, m.video.queue.size
        );
    }

    if (audio) |a| {
        _ = c.wmove(info, 15, 0);
        _ = c.wprintw(info,
            "audio:\n" ++
            "  frame: %4d/%4d: pts: %7ld dt: %7ld\n" ++
            "  queue: start: %2ld size: %2ld\n",
            a.*.key_frame, m.audio.codec_ctx.*.frame_number, a.*.pts, m.audio.dt,
            m.audio.queue.start, m.audio.queue.size
        );
    }
}
