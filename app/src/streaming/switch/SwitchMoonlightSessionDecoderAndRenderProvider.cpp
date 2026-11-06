#include "SwitchMoonlightSessionDecoderAndRenderProvider.hpp"
#include "FFmpegVideoDecoder.hpp"

#ifdef __SWITCH__
#include "AudrenAudioRenderer.hpp"
#endif

#if defined(__SDL2__) || defined(__SDL3__)
#include "SDLAudiorenderer.hpp"
#endif

#ifdef BOREALIS_USE_DEKO3D
#include "DKVideoRenderer.hpp"
#elif defined(USE_D3D11_RENDERER)
#include "D3D11VideoRenderer.hpp"
#else
#error No renderer selected: build for Switch (deko3d) or Windows (USE_D3D11)
#endif

IFFmpegVideoDecoder*
SwitchMoonlightSessionDecoderAndRenderProvider::video_decoder() {
    return new FFmpegVideoDecoder();
}

IVideoRenderer*
SwitchMoonlightSessionDecoderAndRenderProvider::video_renderer() {
#ifdef BOREALIS_USE_DEKO3D
    return new DKVideoRenderer();
#elif defined(USE_D3D11_RENDERER)
    return new D3D11VideoRenderer();
#endif
}

IAudioRenderer*
SwitchMoonlightSessionDecoderAndRenderProvider::audio_renderer() {
#ifdef __SWITCH__
    return new AudrenAudioRenderer();
#elif defined(__SDL2__) || defined(__SDL3__)
    return new SDLAudioRenderer();
#else
#error No audio renderer selected
#endif
}
