// TODO: zig-ify

const std = @import("std");

const c = @cImport({
    //@cInclude("miniaudio.h");

    @cInclude("ncursesw/ncurses.h");

    @cInclude("libavformat/avformat.h");
    @cInclude("libavcodec/avcodec.h");
    @cInclude("libavutil/avutil.h");
    @cInclude("libavutil/imgutils.h");
    @cInclude("libswscale/swscale.h");

    @cInclude("time.h");
});

extern var av_log_pad: ?*c.WINDOW;
extern var av_log_lines: c_int;
extern fn av_log_callback(avcl: ?*anyopaque, level: c_int, fmt: [*c]const u8, [*c]@typeInfo(c.va_list).Array.child) callconv(.C) void;

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const alloc = gpa.allocator();

    const args = try std.process.argsAlloc(alloc);
    defer std.process.argsFree(alloc, args);

    //const stdin = std.io.getStdIn().reader();
    //const stdout = std.io.getStdOut().writer();

    if (args.len != 2) {
        try std.io.getStdErr().writer().print("usage: {s} [file]\n", .{std.fs.path.basename(args[0])});
        return;
    }

    var player = Player.init();
    defer player.deinit();

    av_log_lines = 0;
    player.log_cur_line = 0;
    player.log_snap_end = true;
    c.av_log_set_callback(av_log_callback);
    c.av_log_set_level(c.AV_LOG_TRACE);

    var media = Media.open(args[1]) catch {
        _ = c.getch();
        return;
    };
    defer media.close();
    //_ = c.av_seek_frame(media.format_ctx.?, @intCast(media.video.idx), 10000, 0);

    const aspect_ratio = @as(f32, @floatFromInt(media.video.s.?.*.codecpar.*.width)) / @as(f32, @floatFromInt(media.video.s.?.*.codecpar.*.height));
    player.resize(aspect_ratio);

    media.set_video_size(player.width, player.height);
    media.print_info(player.tab[1]);
    _ = c.wprintw(player.tab[1], "\nplayer size (pixels): %dx%d\n", player.width, player.height);
    _ = c.wprintw(player.tab[1], "screen size (chars): %dx%d", c.COLS, c.LINES);
    player.draw_tab_bar();
    _ = c.refresh();

    // main loop
    var ch = c.wgetch(player.win);
    while (ch != 'q') : (ch = c.wgetch(player.win)) {
    // scrolling
        if (player.win == av_log_pad) {
            const last_page_line: usize = if (av_log_lines > c.LINES) @intCast(av_log_lines - c.LINES) else 0;
            switch (ch) {
                ctrl('u'), c.KEY_PPAGE => {
                    if (player.log_cur_line > 10) {
                        player.log_cur_line -= 10;
                    } else {
                        player.log_cur_line = 0;
                    }
                    player.log_snap_end = false;
                },
                ctrl('d'), c.KEY_NPAGE => {
                    if (last_page_line >= 10 and player.log_cur_line < last_page_line - 10) {
                        player.log_cur_line += 10;
                    } else {
                        player.log_cur_line = last_page_line;
                    }
                    player.log_snap_end = false;
                },
                'g' => {
                    player.log_cur_line = 0;
                    player.log_snap_end = false;
                },
                'G' => {
                    player.log_cur_line = last_page_line;
                    player.log_snap_end = false;
                },
                else => {}
            }

            if (player.log_snap_end) {
                player.log_cur_line = last_page_line;
            } else if (player.log_cur_line == last_page_line) {
                player.log_snap_end = true;
            }
        }
        if (ch == '\t') {
            player.toggle_tab();
            _ = c.refresh();
        }

        const result = media.decode_frame();
        player.current_video_frame = player.current_video_frame orelse media.video.queue.peek();
        player.current_audio_frame = player.current_audio_frame orelse media.audio.queue.peek();

        if (result == error.av_read_frame 
            and player.current_video_frame == null
            and player.current_audio_frame == null) {
            break;
        }

        if (player.current_video_frame) |video| {
            if (media.video.dt >= video.*.pts) {
                player.draw();
                media.video.queue.pop();
                player.current_video_frame = null;
            }
        }
        media.print_frame_info(player.tab[1], player.current_video_frame, player.current_audio_frame);

        if (player.current_audio_frame) |audio| {
            if (media.audio.dt >= audio.*.pts) {
                media.audio.queue.pop();
                player.current_audio_frame = null;
            }
        }

        if (player.win == av_log_pad) {
            _ = c.prefresh(av_log_pad, @intCast(player.log_cur_line), 0, 1, 0, c.LINES - 1, c.COLS);
        } else {
            _ = c.wrefresh(player.win);
        }
    }
}

fn ctrl(comptime ch: u8) u8 {
    return ch & 0x1f;
}

const Player = struct {
    const windows = 3;
    width: usize,
    height: usize,

    tab: [windows]*c.WINDOW,
    tab_name: [windows][:0]const u8,

    win_idx: usize,
    win: *c.WINDOW,

    current_video_frame: ?*c.AVFrame,
    current_audio_frame: ?*c.AVFrame,

    log_cur_line: usize,
    log_snap_end: bool,

    const LOG_MAX_LINES = 10000;

    pub fn init() Player {
        _ = c.initscr(); _ = c.cbreak(); _ = c.noecho(); _ = c.nonl();
        _ = c.curs_set(0);
	_ = c.start_color(); _ = c.use_default_colors();
        inline for (0..256) |i| _ = c.init_pair(i, i, -1);

        var player: Player = undefined;

        player.height = @intCast(c.LINES - 1);
        player.width = @intCast(c.COLS);

        player.tab = .{
            c.newwin(c.LINES - 1, c.COLS, 1, 0).?,
            c.newwin(c.LINES - 1, c.COLS, 1, 0).?,
            c.newpad(LOG_MAX_LINES, c.COLS).?,
        };
        player.tab_name = .{
            "player",
            "info",
            "logs",
        };
        for (player.tab) |tab| {
            _ = c.nodelay(tab, true);
            _ = c.keypad(tab, true);
        }
        av_log_pad = player.tab[2];
        player.win_idx = 0;
        player.win = player.tab[player.win_idx];

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

        const win = self.tab[0];

        const win_start = @as(usize, @intCast(@divTrunc(c.COLS, 2))) - self.width;
        const pixel = video.*.data[0];
        const linesize = video.*.linesize[0];

        _ = c.wmove(win, 0, @intCast(win_start));
        for (0..self.width * self.height) |i| {
            const idx = (i / self.width) * @as(usize, @intCast(linesize)) + (i % self.width) * 3;

            const brightness: usize = @intFromFloat((
                0.299 * @as(f32, @floatFromInt(pixel[idx]))     + // r
                0.587 * @as(f32, @floatFromInt(pixel[idx + 1])) + // g
                0.114 * @as(f32, @floatFromInt(pixel[idx + 2]))   // b
            ) / 256.0 * 13.0); // scale [0, 256) to [0, 13)

            //c.attrset(COLOR_PAIR(rgb2ansi(pixel)));
            const ch = " .,-~:;=!*#$@"[brightness];
            _ = c.wprintw(win, "%c%c", ch, ch);
            //_ = c.attrset(c.A_NORMAL);

            if ((i + 1) % self.width == 0) {
                _ = c.wmove(win, c.getcury(win) + 1, @intCast(win_start));
            }
        }
    }

    pub fn toggle_tab(self: *Player) void {
        self.win_idx = (self.win_idx + 1) % self.tab.len;
        self.win = self.tab[self.win_idx];
        _ = c.redrawwin(self.win);
        self.draw_tab_bar();
    }

    pub fn draw_tab_bar(self: Player) void {
        _ = c.move(0, c.COLS - 20);
        for (0..self.tab.len) |i| {
            if (self.tab[i] == self.win) {
                _ = c.attrset(c.A_REVERSE);
            }
            _ = c.printw("%s", self.tab_name[i].ptr);
            _ = c.attrset(c.A_NORMAL);
            _ = c.printw("  ");
        }
    }

    pub fn resize(self: *Player, aspect_ratio: f32) void {
	self.height = @intCast(c.LINES - 1);
        self.width = @intFromFloat(@as(f32, @floatFromInt(self.height)) * aspect_ratio);
    }
};

const Media = struct {
    url: []const u8,
    format_ctx: ?*c.AVFormatContext,
    video: Track,
    audio: Track,

    const QUEUE_SIZE = 32;
    const FrameQueue = struct {
        frame: [QUEUE_SIZE]*c.AVFrame,
        start: usize,
        size: usize,

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

    pub fn open(url: [:0]const u8) !Media {
        var m: Media = undefined;
        m.format_ctx = null;
        m.url = url;
        if (c.avformat_open_input(@ptrCast(&m.format_ctx), @ptrCast(m.url), null, null) < 0) {
            _ = c.printw("error: failed to open \'%s\'\n", m.url.ptr);
            return error.open;
        }

        _ = c.avformat_find_stream_info(m.format_ctx.?, null);
        m.video.s = null;
        m.audio.s = null;

        for (0..m.format_ctx.?.*.nb_streams) |i| {
            const stream: *c.AVStream = m.format_ctx.?.*.streams[i];

            if (m.video.s == null and stream.codecpar.*.codec_type == c.AVMEDIA_TYPE_VIDEO) {
                m.video.s = stream;
                m.video.idx = @intCast(i);
                m.video.open_codec();
            } else if (m.audio.s == null and stream.codecpar.*.codec_type == c.AVMEDIA_TYPE_AUDIO) {
                m.audio.s = stream;
                m.audio.idx = @intCast(i);
                m.audio.open_codec();
            }
        }

        if (av_frame == null) {
            av_frame = c.av_frame_alloc();
        }
        if (av_packet == null) {
            av_packet = c.av_packet_alloc();
        }
        m.video.queue.size = 0;
        m.video.queue.start = 0;
        m.audio.queue.size = 0;
        m.audio.queue.start = 0;
        for (0..QUEUE_SIZE) |i| {
            m.video.queue.frame[i] = c.av_frame_alloc();
            _ = c.av_image_alloc(&m.video.queue.frame[i].*.data, &m.video.queue.frame[i].*.linesize,
                m.video.s.?.*.codecpar.*.width, m.video.s.?.*.codecpar.*.height,
                c.AV_PIX_FMT_RGB24, @intCast(c.av_cpu_max_align())
            );
            m.audio.queue.frame[i] = c.av_frame_alloc();
        }
        c.sws_freeContext(sws_ctx);
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
            c.av_frame_free(@ptrCast(&m.video.queue.frame[i]));
        }
        for (0..QUEUE_SIZE) |i| {
            c.av_frame_free(@ptrCast(&m.audio.queue.frame[i]));
        }

        c.av_packet_free(&av_packet);
        c.av_frame_free(&av_frame);
        c.sws_freeContext(sws_ctx);
        sws_ctx = null;

        c.avcodec_free_context(@ptrCast(&m.video.codec_ctx));
        c.avcodec_free_context(@ptrCast(&m.audio.codec_ctx));
        c.avformat_close_input(@ptrCast(&m.format_ctx));

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

    pub fn print_info(m: Media, win: *c.WINDOW) void {
        _ = c.wprintw(win,
            "%s:\nformat: %s, duration: %ld\n",
            m.url.ptr, m.format_ctx.?.*.iformat.*.long_name, m.format_ctx.?.*.duration
        );
        _ = c.wprintw(win,
            "video (%d):\n" ++
            "  resolution: %dx%d, frame rate: %d/%d, time base: %d/%d\n" ++
            "  codec: %s, bit rate: %ld\n",
            m.video.idx, m.video.s.?.*.codecpar.*.width, m.video.s.?.*.codecpar.*.height,
            m.video.s.?.*.avg_frame_rate.num, m.video.s.?.*.avg_frame_rate.den,
            m.video.s.?.*.time_base.num, m.video.s.?.*.time_base.den, m.video.codec.*.long_name, m.video.s.?.*.codecpar.*.bit_rate
        );

        _ = c.wprintw(win,
            "audio (%d):\n" ++
            "  channels: %d, sample rate: %d, time base: %d/%d\n" ++
            "  codec: %s, bit rate: %ld\n",
            m.audio.idx, m.audio.s.?.*.codecpar.*.channels, m.audio.s.?.*.codecpar.*.sample_rate,
            m.audio.s.?.*.time_base.num, m.audio.s.?.*.time_base.den,
            m.audio.codec.*.long_name, m.audio.s.?.*.codecpar.*.bit_rate
        );
    }

    pub fn print_frame_info(m: Media, win: *c.WINDOW, video: ?*c.AVFrame, audio: ?*c.AVFrame) void {
        _ = c.wmove(win, 12, 0);
        _ = c.wprintw(win, "video:\n");
        if (video) |v|
            _ = c.wprintw(win, "  frame: %4d/", v.*.key_frame)
        else
            _ = c.wprintw(win, "  frame:     /");
        _ = c.wprintw(win, "%4d: ", m.video.codec_ctx.*.frame_number);
        if (video) |v|
            _ = c.wprintw(win, "pts: %6ld ", v.*.pts)
        else
            _ = c.wprintw(win, "pts:        ");
        _ = c.wprintw(win, "dt: %6ld\n", m.video.dt);
        _ = c.wprintw(win, "  queue: start: %2ld size: %2ld\n", m.video.queue.start, m.video.queue.size);
    
        _ = c.wmove(win, 15, 0);
        _ = c.wprintw(win, "audio:\n");
        if (audio) |a|
            _ = c.wprintw(win, "  frame: %4d/", a.*.key_frame)
        else
            _ = c.wprintw(win, "  frame:     /");
        _ = c.wprintw(win, "%4d: ", m.audio.codec_ctx.*.frame_number);
        if (audio) |a|
            _ = c.wprintw(win, "pts: %6ld ", a.*.pts)
        else
            _ = c.wprintw(win, "pts:        ");
        _ = c.wprintw(win, "dt: %6ld\n", m.audio.dt);
        _ = c.wprintw(win, "  queue: start: %2ld size: %2ld\n", m.audio.queue.start, m.audio.queue.size);
    }

    pub fn decode_frame(m: *Media) !void {
        if (m.video.queue.size < QUEUE_SIZE and m.audio.queue.size < QUEUE_SIZE) {
            if (c.av_read_frame(m.format_ctx.?, av_packet) < 0) {
                return error.av_read_frame;
            }
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
