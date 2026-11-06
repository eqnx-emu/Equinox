#pragma once


#include "IFFmpegVideoDecoder.hpp"
#include "AVFrameHolder.hpp"
#include "FFmpegVideoDecoderPlatformHelpers.hpp"

class FFmpegVideoDecoder : public IFFmpegVideoDecoder {
  public:
    FFmpegVideoDecoder();
    ~FFmpegVideoDecoder();

    int setup(int video_format, int width, int height, int redraw_rate,
              void* context, int dr_flags) override;
    void cleanup() override;
    int submit_decode_unit(PDECODE_UNIT decode_unit) override;
    int capabilities() const override;
    VideoDecodeStats* video_decode_stats() override;

  private:
    int decode(char* indata, int inlen);
    int drain_frames();
    int get_frame(bool native_frame, AVFrame** frame);
    int configure_decoder_context(bool enable_hw_decode, bool enable_low_delay,
                                  bool enable_decoder_threads);
    int open_decoder();
    int finalize_decoder_setup();

    AVPacket* m_packet;
    AVBufferRef *hw_device_ctx = nullptr;
    const AVCodec* m_decoder = nullptr;
    AVCodecContext* m_decoder_context = nullptr;
    AVFrame *tmp_frame = nullptr;
    AVFrame** m_frames = nullptr;
    int m_frames_size = 0;

    int m_stream_fps = 0;
    int m_video_format = 0;
    int m_video_width = 0;
    int m_video_height = 0;
    int m_perf_level = 0;
    int m_decoder_threads_setting = 1;
    int m_frames_in = 0;
    int m_frames_out = 0;
    int m_current_frame = 0, m_next_frame = 0;
    uint32_t m_last_frame = 0;
    AVCodecID m_codec_id = AV_CODEC_ID_NONE;

    bool m_hw_decode_active = false;
    bool m_supports_slice_threading = false;
    bool m_use_decoder_threads = false;
    bool m_use_low_delay = false;
    bool m_decoder_ready = false;
    bool m_decoder_finalized = false;

    VideoDecodeStats m_video_decode_stats_progress = {};
    VideoDecodeStats m_video_decode_stats_cache = {};
    uint64_t m_stats_window_start = 0;
    int m_consecutive_decode_errors = 0;
    bool m_reconnect_requested = false;
    size_t m_ffmpeg_buffer_capacity = 0;

    char* m_ffmpeg_buffer = nullptr;
    bool m_use_zero_copy_holder = false;
#if defined(_WIN32) && defined(USE_D3D11_RENDERER)
  ffmpeg::decoder::D3D11State m_d3d11;
#endif
};
