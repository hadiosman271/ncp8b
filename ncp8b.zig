const std = @import("std");

const c = @cImport({
    @cInclude("miniaudio.h");
    @cInclude("ncursesw/curses.h");

    @cInclude("libavformat/avformat.h");
    @cInclude("libavcodec/avcodec.h");
    @cInclude("libavutil/avutil.h");
    @cInclude("libavutil/cpu.h");
    @cInclude("libavutil/imgutils.h");
    @cInclude("libswscale/swscale.h");
});

pub fn main() !void {
    //var gpa_config = std.heap.GeneralPurposeAllocator(.{}){};
    //const gpa = gpa_config.allocator();

    //const args = try std.process.argsAlloc(gpa);
    //defer std.process.argsFree(gpa, args);

    //const stdin = std.io.getStdIn().reader();
    //const stdout = std.io.getStdOut().writer();

    var engine: c.ma_engine = undefined;
    if (c.ma_engine_init(null, &engine) != c.MA_SUCCESS) {
        return error.ma_engine_init;
    }
    defer c.ma_engine_uninit(&engine);
    if (c.ma_engine_play_sound(&engine, "song.mp3", null) != c.MA_SUCCESS) {
        return error.ma_engine_play_sound;
    }

    c.av_log_set_level(c.AV_LOG_QUIET);
    var m = try Media.open("video.mp4");
    m.print_info();
    m.close();

    _ = c.initscr(); _ = c.cbreak(); _ = c.noecho(); _ = c.nonl();
    defer _ = c.endwin();
    _ = c.printw("q to exit\n");
    var ch = c.getch();
    while (ch != 'q') : (ch = c.getch()) {
        _ = c.printw("not \'q\'\n");
    }
}


const Media = struct {
    url: []const u8,
    format_ctx: [*c]c.AVFormatContext,
    video: Track,
    audio: Track,

    const QUEUE_SIZE = 32;
    const FrameQueue = struct {
        frame: [QUEUE_SIZE][*c]c.AVFrame,
        start: usize, size: usize
    };

    const Track = struct {
        s: [*c]c.AVStream,
        idx: isize,
        codec_ctx: [*c]c.AVCodecContext,
        codec: [*c]const c.AVCodec,
        queue: FrameQueue,
        //t1: u64, dt: u64,

        pub fn open_codec(t: *Track) void {
            t.codec = c.avcodec_find_decoder(t.s.*.codecpar.*.codec_id);
            t.codec_ctx = c.avcodec_alloc_context3(t.codec);
            _ = c.avcodec_parameters_to_context(t.codec_ctx, t.s.*.codecpar);
            _ = c.avcodec_open2(t.codec_ctx, t.codec, null);
        }
    };


    var av_packet: [*c]c.AVPacket = undefined;
    var av_frame: [*c]c.AVFrame = undefined;
    var sws_ctx: ?*c.SwsContext = undefined;

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
                m.video.s.*.codecpar.*.width, m.video.s.*.codecpar.*.height,
        	c.AV_PIX_FMT_RGB24, @as(c_int, @intCast(c.av_cpu_max_align()))
            );
            m.audio.queue.frame[i] = c.av_frame_alloc();
        }
        sws_ctx = c.sws_getContext(
       	    m.video.s.*.codecpar.*.width, m.video.s.*.codecpar.*.height,
       	    m.video.s.*.codecpar.*.format,
       	    m.video.s.*.codecpar.*.width, m.video.s.*.codecpar.*.height,
       	    c.AV_PIX_FMT_RGB24, 0, null, null, null
        );

        //const t: c.clock_t = c.clock();
        //m.video.t1 = t * m.video.s.*.time_base.den / c.CLOCKS_PER_SEC;
        //m.audio.t1 = t * m.audio.s.*.time_base.den / c.CLOCKS_PER_SEC;

        return m;
    }

    pub fn close(m: *Media) void {
    	for (0..QUEUE_SIZE) |i| {
    	    c.av_freep(@as(?*anyopaque, @ptrCast(&m.video.queue.frame[i].*.data[0])));
    	    c.av_frame_free(&m.video.queue.frame[i]);
    	}
    	for (0..QUEUE_SIZE) |i| {
    	    c.av_frame_free(&m.audio.queue.frame[i]);
    	}

    	c.av_packet_free(&av_packet);
    	c.av_frame_free(&av_frame);
    	c.sws_freeContext(sws_ctx);

    	c.avcodec_free_context(&m.video.codec_ctx);
    	c.avcodec_free_context(&m.audio.codec_ctx);
    	c.avformat_close_input(&m.format_ctx);

        m.* = undefined;
    }

    pub fn print_info(m: Media) void {
    	std.debug.print("{s}:\nformat: {s}, duration: {d}\n",
            .{ m.url, m.format_ctx.*.iformat.*.long_name, m.format_ctx.*.duration }
    	);
    	std.debug.print(
            "video ({d}):\n" ++
    	    "  resolution: {d}x{d}, frame rate: {d}/{d}, time base: {d}/{d}\n" ++
    	    "  codec: {s}, bit rate: {d}\n", .{
            m.video.idx, m.video.s.*.codecpar.*.width, m.video.s.*.codecpar.*.height,
    	    m.video.s.*.avg_frame_rate.num, m.video.s.*.avg_frame_rate.den,
    	    m.video.s.*.time_base.num, m.video.s.*.time_base.den,
    	    m.video.codec.*.long_name, m.video.s.*.codecpar.*.bit_rate
        });
    
    	std.debug.print(
            "audio ({d}):\n" ++
    	    "  channels: {d}, sample rate: {d}, time base: {d}/{d}\n" ++
    	    "  codec: {s}, bit rate: {d}\n", .{
            m.audio.idx, m.audio.s.*.codecpar.*.ch_layout.nb_channels, m.audio.s.*.codecpar.*.sample_rate,
    	    m.audio.s.*.time_base.num, m.audio.s.*.time_base.den,
    	    m.audio.codec.*.long_name, m.audio.s.*.codecpar.*.bit_rate
        });
    }
};
