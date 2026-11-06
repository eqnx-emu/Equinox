#include "FFmpegVideoDecoderPlatformHelpers.hpp"

#include <algorithm>
#include <Limelight.h>

#include "AVFrameHolder.hpp"
#include "Settings.hpp"
#include "borealis.hpp"

#if defined(_WIN32) && defined(USE_D3D11_RENDERER)
extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/pixdesc.h>
}

#include <borealis/core/application.hpp>
#include <borealis/platforms/driver/d3d11.hpp>
#include <borealis/platforms/sdl/sdl_video.hpp>

namespace ffmpeg::decoder {

namespace {

enum AVPixelFormat selectD3D11HardwareFormat(AVCodecContext* avctx, const enum AVPixelFormat* pix_fmts) {
    auto* state = static_cast<D3D11State*>(avctx->opaque);
    if (state == nullptr) {
        return avcodec_default_get_format(avctx, pix_fmts);
    }

    state->zeroCopyActive = false;

    for (const enum AVPixelFormat* format = pix_fmts; *format != AV_PIX_FMT_NONE; ++format) {
        if (*format != state->hwPixelFormat) {
            continue;
        }

        const int err = configureD3D11FramesContext(*state, avctx, *format);
        if (err >= 0) {
            state->zeroCopyActive = state->deviceSharedWithRenderer;
            return *format;
        }

        char error[AV_ERROR_MAX_STRING_SIZE] = {0};
        brls::Logger::warning("FFmpeg: D3D11 zero-copy surface setup failed - {}",
            av_make_error_string(error, AV_ERROR_MAX_STRING_SIZE, err));

        state->hwPixelFormat = AV_PIX_FMT_NONE;
        av_buffer_unref(&avctx->hw_frames_ctx);
        av_buffer_unref(&avctx->hw_device_ctx);
        break;
    }

    if (state->hwSurfaceFormat != AV_PIX_FMT_NONE) {
        for (const enum AVPixelFormat* format = pix_fmts; *format != AV_PIX_FMT_NONE; ++format) {
            if (*format == state->hwSurfaceFormat) {
                brls::Logger::warning("FFmpeg: Falling back from D3D11 zero-copy to software pixel format {}",
                    av_get_pix_fmt_name(*format));
                return *format;
            }
        }
    }

    for (const enum AVPixelFormat* format = pix_fmts; *format != AV_PIX_FMT_NONE; ++format) {
        const AVPixFmtDescriptor* descriptor = av_pix_fmt_desc_get(*format);
        if (descriptor != nullptr && (descriptor->flags & AV_PIX_FMT_FLAG_HWACCEL) == 0) {
            brls::Logger::warning("FFmpeg: Falling back from D3D11 zero-copy to software pixel format {}",
                av_get_pix_fmt_name(*format));
            return *format;
        }
    }

    return avcodec_default_get_format(avctx, pix_fmts);
}

} // namespace

void resetD3D11State(D3D11State& state) {
    state.zeroCopyActive = false;
    state.deviceSharedWithRenderer = false;
    state.unavailableReason = nullptr;
    state.hwPixelFormat = AV_PIX_FMT_NONE;
    state.hwSurfaceFormat = AV_PIX_FMT_NONE;
}

void prepareD3D11Setup(D3D11State& state, int video_format) {
    state.hwPixelFormat = AV_PIX_FMT_D3D11;
    state.hwSurfaceFormat = (video_format & VIDEO_FORMAT_MASK_10BIT) ? AV_PIX_FMT_P010 : AV_PIX_FMT_NV12;
}

int initializeD3D11HardwareDevice(D3D11State& state, AVBufferRef*& hw_device_ctx) {
    // Decode on a device of its own and copy frames to system memory for the
    // renderer. Sharing the renderer's immediate context with the decoder
    // thread made decodes wait on the UI thread (tens of ms per frame) and,
    // without multithread protection, hang inside the driver when the stream
    // was stopped. This build is for local testing, so simple beats zero-copy.
    state.deviceSharedWithRenderer = false;
    state.unavailableReason = nullptr;

    const int err = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_D3D11VA,
                                           nullptr, nullptr, 0);
    if (err < 0) {
        hw_device_ctx = nullptr;
        state.unavailableReason = "no D3D11 video decode device";
    }
    return err;
}

int configureD3D11FramesContext(D3D11State& state, AVCodecContext* avctx, enum AVPixelFormat hwPixelFormat) {
    if (avctx->hw_device_ctx == nullptr) {
        return AVERROR(EINVAL);
    }

    AVBufferRef* framesRef = nullptr;
    int err = avcodec_get_hw_frames_parameters(avctx, avctx->hw_device_ctx, hwPixelFormat, &framesRef);
    if (err < 0) {
        return err;
    }

    auto* framesContext = reinterpret_cast<AVHWFramesContext*>(framesRef->data);
    auto* d3d11FramesContext = reinterpret_cast<AVD3D11VAFramesContext*>(framesContext->hwctx);

    framesContext->sw_format = state.hwSurfaceFormat;
    framesContext->initial_pool_size = std::max(
        framesContext->initial_pool_size,
        static_cast<int>(AVFrameQueue::capacityFor(
            Settings::instance().frames_queue_size())) +
            avctx->thread_count + 2);
    d3d11FramesContext->BindFlags |= D3D11_BIND_SHADER_RESOURCE;

    err = av_hwframe_ctx_init(framesRef);
    if (err >= 0) {
        av_buffer_unref(&avctx->hw_frames_ctx);
        avctx->hw_frames_ctx = av_buffer_ref(framesRef);
        if (avctx->hw_frames_ctx == nullptr) {
            err = AVERROR(ENOMEM);
        }
    }

    av_buffer_unref(&framesRef);
    return err;
}

void configureD3D11DecoderContext(D3D11State& state, AVCodecContext* decoderContext) {
    decoderContext->opaque = &state;
    decoderContext->get_format = &selectD3D11HardwareFormat;
    decoderContext->extra_hw_frames = static_cast<int>(AVFrameQueue::capacityFor(
        Settings::instance().frames_queue_size()));
}

void logD3D11HardwareInitFailure(const D3D11State& state, const char* error) {
    if (state.unavailableReason != nullptr) {
        brls::Logger::info("FFmpeg: Native D3D11 rendering is active, but {}. Using software video decode on this machine.", state.unavailableReason);
    } else {
        brls::Logger::warning("FFmpeg: Hardware D3D11 decode unavailable on the shared renderer device; continuing with software decode ({})", error);
    }
}

bool useD3D11ZeroCopyHolder(const D3D11State& state) {
    return state.zeroCopyActive;
}

} // namespace ffmpeg::decoder

#endif
