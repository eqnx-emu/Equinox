//
//  streaming_view.cpp
//  Moonlight
//
//  Created by Даниил Виноградов on 27.05.2021.
//

#ifdef __SWITCH__
#include <borealis/platforms/switch/switch_input.hpp>
#endif

#include "streaming_view.hpp"
#include "AVFrameHolder.hpp"
#include "InputManager.hpp"
#include "click_gesture_recognizer.hpp"
#include "helper.hpp"
#include "ingame_overlay_view.hpp"
#include "streaming_input_overlay.hpp"
#include "two_finger_scroll_recognizer.hpp"
#include <Limelight.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <nanovg.h>

#if defined(__SDL3__)
#include <SDL3/SDL.h>
#elif defined(__SDL2__)
#include <SDL2/SDL.h>
#endif

using namespace brls;
using namespace brls::literals;

#ifdef PLATFORM_TVOS
extern void updatePreferredDisplayMode(bool streamActive);
#endif

void setBottomBarStatus(const char *value) {
#if defined(__SDL2__) || defined(__SDL3__)
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, value);
#endif
}

void overrideButtonsIfNeeded(bool value) {
#ifdef PLATFORM_SWITCH
    ((SwitchInputManager*) brls::Application::getPlatform()->getInputManager())->setScreenshotButtonOverrideMode(ButtonOverrideMode::NONE);
    ((SwitchInputManager*) brls::Application::getPlatform()->getInputManager())->setHomeButtonOverrideMode(ButtonOverrideMode::NONE);
    if (!value) return;

    switch (Settings::instance().get_overlay_system_button()) {
        case ButtonOverrideType::NONE: break;
        case ButtonOverrideType::HOME:
            ((SwitchInputManager*) brls::Application::getPlatform()->getInputManager())->setHomeButtonOverrideMode(ButtonOverrideMode::CUSTOM_EVENT);
            break;  
        case ButtonOverrideType::SCREENSHOT:
            ((SwitchInputManager*) brls::Application::getPlatform()->getInputManager())->setScreenshotButtonOverrideMode(ButtonOverrideMode::CUSTOM_EVENT);
            break;
    }

    switch (Settings::instance().get_guide_system_button()) {
        case ButtonOverrideType::NONE: break;
        case ButtonOverrideType::HOME:
            ((SwitchInputManager*) brls::Application::getPlatform()->getInputManager())->setHomeButtonOverrideMode(ButtonOverrideMode::GUIDE_BUTTON);
            break;  
        case ButtonOverrideType::SCREENSHOT:
            ((SwitchInputManager*) brls::Application::getPlatform()->getInputManager())->setScreenshotButtonOverrideMode(ButtonOverrideMode::GUIDE_BUTTON);
            break;
    }
#endif
}

StreamingView::StreamingView(const Host& host, const AppInfo& app) : host(host), app(app) {
    Application::getPlatform()->disableScreenDimming(true);
    MoonlightInputManager::instance().startControllerPolling();

    setFocusable(true);
    setHideHighlight(true);
    loader = new LoadingOverlay(this);

    keyboardHolder = new Box(Axis::COLUMN);
    keyboardHolder->detach();
    keyboardHolder->setJustifyContent(JustifyContent::FLEX_END);
    keyboardHolder->setAlignItems(AlignItems::STRETCH);
    addView(keyboardHolder);

    session = new MoonlightSession(host.preferred_address(), app.app_id);

#ifdef PLATFORM_TVOS
        updatePreferredDisplayMode(true);
#endif

    ASYNC_RETAIN
    GameStreamClient::instance().connect(
        host, [ASYNC_TOKEN](GSResult<SERVER_DATA> result) {
            ASYNC_RELEASE
            if (!result.isSuccess()) {
                showError(result.error(), [this]() { terminate(false); });
                return;
            }

            session->set_address(
                GameStreamClient::instance().active_address(this->host));

            ASYNC_RETAIN
            session->start([ASYNC_TOKEN](GSResult<bool> result) {
                ASYNC_RELEASE

                loader->setHidden(true);
                if (!result.isSuccess()) {
                    showError(result.error(), [this]() { terminate(false); });
                }
            }, result.value().isSunshine());
        });

    // Subscribed once here, not in onFocusGained, which runs again every time
    // an overlay closes.
    windowFocusSubscription =
        Application::getWindowFocusChangedEvent()->subscribe(
            [this](bool focused) { this->onWindowFocusChanged(focused); });

    MoonlightInputManager::instance().reloadButtonMappingLayout();

    static bool lMouseKeyGate = false;
    static bool lMouseKeyUsed = false;
    addGestureRecognizer(new FingersGestureRecognizer([](){
                             return Settings::instance().get_keyboard_fingers();
                         }, [this] { addKeyboard(); }));

    addGestureRecognizer(
        new ClickGestureRecognizer(1, [](TapGestureStatus status) {
            if (Settings::instance().touchscreen_mouse_mode()) return;

            if (status.state == brls::GestureState::END) {
                Logger::debug("Left mouse click");
                MoonlightInputManager::leftMouseClick();
                lMouseKeyGate = true;
                delay(200, [] { lMouseKeyGate = false; });
            }
        }));

    addGestureRecognizer(
        new ClickGestureRecognizer(2, [](TapGestureStatus status) {
            if (Settings::instance().touchscreen_mouse_mode()) return;

            if (status.state == brls::GestureState::END) {
                Logger::debug("Right mouse click");
                MoonlightInputManager::rightMouseClick();
            }
        }));

    addGestureRecognizer(new PanGestureRecognizer(
        [this](PanGestureStatus status, Sound* sound) {
            static bool overlayTriggered = false;

            // Close keyboard by swiping outside of it
            if (status.state == brls::GestureState::START) {
                removeKeyboard();
                overlayTriggered = false;
            }

            // Open overlay by swipe from left screen corner
            bool hasControllers = Application::getPlatform()->getInputManager()->getControllersConnectedCount() > 0;
            if (!hasControllers && !overlayTriggered && status.state == brls::GestureState::STAY && status.startPosition.x < 100 && status.position.x > 200) {
                overlayTriggered = true;
                auto overlay = new IngameOverlay(this);
                Application::pushActivity(new Activity(overlay));
            }

            if (Settings::instance().touchscreen_mouse_mode()) return;

            if (status.state == brls::GestureState::UNSURE && lMouseKeyGate) {
                lMouseKeyGate = false;
                lMouseKeyUsed = true;
            } else if (status.state == brls::GestureState::START) {
                if (lMouseKeyUsed) {
//                    Logger::debug("Pressed key at {}", status.state);
                    LiSendMouseButtonEvent(BUTTON_ACTION_PRESS,
                                           BUTTON_MOUSE_LEFT);
                }
            } else if (status.state == brls::GestureState::STAY) {
                brls::RawMouseState mouseState;
                Application::getPlatform()->getInputManager()->updateMouseStates(&mouseState);
                // Dirty hack to not update pan if mouse left button is pressed, because pan gesture recognizer will append its speed with raw mouse value
                // Need to improve gesture recognizers to determine the input source and ignore it for mouse
                if (!mouseState.leftButton) {
                    MoonlightInputManager::instance().updateTouchScreenPanDelta(
                            status);
                }
            } else if (lMouseKeyUsed) {
//                Logger::debug("Release key at {}", status.state);
                LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE,
                                       BUTTON_MOUSE_LEFT);
                lMouseKeyUsed = false;
            }
        },
        PanAxis::ANY));

    scrollTouchRecognizer = new TwoFingerScrollGestureRecognizer(
            [this](TwoFingerScrollState state) {
                if (Settings::instance().touchscreen_mouse_mode()) return;

                if (state.state == brls::GestureState::START)
                    this->touchScrollCounter = 0;

                int threshold = int(state.delta.y / 25);
                if (threshold != this->touchScrollCounter) {
                    Logger::debug("Scroll on: {}",
                                  threshold - this->touchScrollCounter);
                    int invert = Settings::instance().swap_mouse_scroll() ? -1 : 1;
                    char scrollCount = threshold - this->touchScrollCounter;
                    LiSendScrollEvent(scrollCount * invert);
                    this->touchScrollCounter = threshold;
                }
            });
    addGestureRecognizer(scrollTouchRecognizer);

    keysSubscription =
        Application::getPlatform()
            ->getInputManager()
            ->getKeyboardKeyStateChanged()
            ->subscribe([this, host, app](brls::KeyState state) {
                if (state.key == BRLS_KBD_KEY_ESCAPE) {
                    static std::chrono::high_resolution_clock::time_point
                        clock_counter;
                    static bool buttonState = false;
                    static bool used = false;

                    auto duration =
                        std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::high_resolution_clock::now() -
                            clock_counter);

                    if (!buttonState && state.pressed) {
                        buttonState = true;
                        clock_counter =
                            std::chrono::high_resolution_clock::now();
                    } else if (buttonState && !state.pressed) {
                        buttonState = false;
                        used = false;
                    } else if (buttonState && duration.count() >= 2 && !used) {
                        used = true;

                        auto overlay = new IngameOverlay(this);
                        Application::pushActivity(new Activity(overlay));
                    }
                }
            });
}

void StreamingView::onFocusGained() {
    Box::onFocusGained();

    MoonlightInputManager::instance().setInputEnabled(true);

    if (!blocked) {
        blocked = true;
        Application::blockInputs(true);
    }

    tempInputLock = true;
    ASYNC_RETAIN
    delay(300, [ASYNC_TOKEN]() {
        ASYNC_RELEASE
        this->tempInputLock = false;
    });

    Application::getPlatform()->getInputManager()->setPointerLock(true);

    overrideButtonsIfNeeded(true);
    setBottomBarStatus("1");

    scrollTouchRecognizer->forceReset();
}

void StreamingView::onFocusLost() {
    Box::onFocusLost();

    MoonlightInputManager::instance().setInputEnabled(false);
    MoonlightInputManager::instance().dropInput();

    if (blocked) {
        blocked = false;
        Application::unblockInputs();
    }

    removeKeyboard();
    Application::getPlatform()->getInputManager()->setPointerLock(false);

    overrideButtonsIfNeeded(false);
    setBottomBarStatus("2");

    if (bottombarDelayTask != -1)
        cancelDelay(bottombarDelayTask);
}

void StreamingView::onWindowFocusChanged(bool focused) {
    if (windowFocused == focused)
        return;

    windowFocused = focused;
    Logger::info("StreamingView: window focus {}", focused ? "gained" : "lost");

    if (!focused) {
        // HOME menu or console sleep: stop sending input and stop rendering
        // before the graphics service goes away underneath the session.
        MoonlightInputManager::instance().setControllerPollingActive(false);
        MoonlightInputManager::instance().dropInput();
    }

    session->set_suspended(!focused);
}

void StreamingView::draw(NVGcontext* vg, float x, float y, float width,
                         float height, Style style, FrameContext* ctx) {
    // Off screen, run nothing: no stream, input or overlays. This comes before
    // the termination check so a session dropped because the console turned
    // wifi off for sleep is not torn down while the compositor is stopped; the
    // check below picks it up on the first frame back.
    if (!windowFocused)
        return;

    if (session->is_terminated()) {
        terminate(false);
        return;
    }

    MoonlightInputManager::instance().setControllerPollingActive(
        !tempInputLock && session->is_active() && this->focused);

#ifdef __SWITCH__
    if (Settings::instance().frames_queue_size() <= 1)
        latchBeforeVsync();

    // Resolution/bitrate are negotiated at connect time, so a dock change
    // mid-stream can only take effect on the next connection.
    const int handheldMode = Settings::is_handheld() ? 1 : 0;
    if (lastHandheldMode != -1 && handheldMode != lastHandheldMode &&
        Settings::instance().mode_profiles() &&
        (handheldMode == 1) != session->handheld_profile()) {
        brls::Application::notify("streaming/profile_reconnect_hint"_i18n);
    }
    lastHandheldMode = handheldMode;
#endif

    session->draw(vg, (int) width, (int) height);

    if (!tempInputLock && session->is_active())
        handleInput();
    handleOverlayCombo();
    handleMouseInputCombo();

    if (session->connection_status_is_poor()) {
        // moonlight-common-c reports "poor" for any late frame, including ones
        // late because the local decoder can't keep up. Tell them apart.
        auto decodeStats = session->session_stats()->video_decode_stats;
        const float frameBudgetMs =
            1000.0f / (float) std::max(Settings::instance().fps(), 1);
        const bool decoderOverloaded =
            decodeStats.current_decoding_time > frameBudgetMs * 0.9f ||
            (decodeStats.current_received_fps > 0 &&
             decodeStats.current_decoded_fps <
                 decodeStats.current_received_fps * 0.9f);
        const std::string warning =
            "\uE140 " + (decoderOverloaded ? "streaming/decoder_overloaded"_i18n
                                           : "streaming/bad_connection"_i18n);

        nvgFontSize(vg, 20);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

        nvgFontBlur(vg, 3);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
        nvgFontFaceId(vg, Application::getFont(FONT_REGULAR));
        nvgText(vg, 50, height - 28, warning.c_str(), nullptr);

        nvgFontBlur(vg, 0);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgFontFaceId(vg, Application::getFont(FONT_REGULAR));
        nvgText(vg, 50, height - 28, warning.c_str(), nullptr);
    }

    if (session->use_hdr() != m_use_hdr) {
        m_use_hdr = session->use_hdr();

#ifdef PLATFORM_TVOS
        updatePreferredDisplayMode(true);
#endif
    }

    if (draw_stats) {
        // The stats only change a few times a second; formatting them and
        // laying the text out twice (blurred shadow + text) every frame cost
        // a noticeable part of the frame. Rebuild the text every 250 ms and
        // draw it once over a plain backdrop.
        nvgFontFaceId(vg, Application::getFont(FONT_REGULAR));
        nvgFontSize(vg, 20);
        nvgFontBlur(vg, 0);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);

        const auto statsNow = std::chrono::steady_clock::now();
        if (statsText.empty() ||
            statsNow - statsTextTime >= std::chrono::milliseconds(250)) {
            statsText = buildStatsText();
            statsTextTime = statsNow;
            nvgTextBoxBounds(vg, 20, 30, width, statsText.c_str(), nullptr,
                             statsBounds);
        }

        nvgBeginPath(vg);
        nvgRect(vg, statsBounds[0] - 6, statsBounds[1] - 4,
                statsBounds[2] - statsBounds[0] + 12,
                statsBounds[3] - statsBounds[1] + 8);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 170));
        nvgFill(vg);

        nvgFillColor(vg, nvgRGBA(0, 255, 0, 255));
        nvgTextBox(vg, 20, 30, width, statsText.c_str(), nullptr);
    }

    Box::draw(vg, x, y, width, height, style, ctx);
}

std::string StreamingView::buildStatsText() {
    auto stats = session->session_stats();

    const int videoFormat = session->video_format();
    const char* codecName =
        (videoFormat & VIDEO_FORMAT_MASK_AV1)    ? "AV1"
        : (videoFormat & VIDEO_FORMAT_MASK_H265) ? "HEVC"
        : (videoFormat & VIDEO_FORMAT_MASK_H264) ? "H.264"
                                                 : "?";
    auto statistics = fmt::format(
        "Stream: {}x{} @ {} FPS | {}{} | {:.1f} Mbps",
        session->stream_width(), session->stream_height(),
        session->stream_fps(), codecName,
        (videoFormat & VIDEO_FORMAT_MASK_10BIT) ? " 10-bit" : "",
        session->bitrate() / 1000.0f);
#ifdef __SWITCH__
    statistics += Settings::instance().mode_profiles()
                      ? (session->handheld_profile() ? " | Handheld profile"
                                                     : " | Docked profile")
                      : "";
#endif
    statistics += "\n";

    uint32_t rtt = 0, rttVariance = 0;
    const bool hasRtt = LiGetEstimatedRttInfo(&rtt, &rttVariance);
    const auto& ds = stats->video_decode_stats;
    const auto& rs = stats->video_render_stats;
    const float networkMs = hasRtt ? rtt / 2.0f : 0.0f;
    const float queueMs =
        AVFrameHolder::instance().getFrameQueueSize() *
        (1000.0f / (float) std::max(session->stream_fps(), 1));
    const float totalMs = networkMs + ds.current_receive_time +
                          ds.current_decoding_time + queueMs +
                          rs.rendering_time;
    if (hasRtt) {
        statistics += fmt::format("Network RTT: {} ms (variance {} ms)\n",
                                  rtt, rttVariance);
    }
    statistics += fmt::format(
        "Latency estimate: {:.1f} ms (net {:.1f} + recv {:.1f} + decode {:.1f} + queue {:.1f} + render {:.1f})\n",
        totalMs, networkMs, ds.current_receive_time,
        ds.current_decoding_time, queueMs, rs.rendering_time);

    statistics += fmt::format(
                "Estimated host PC frame rate: {:.{}f} FPS\n"
                    "Incoming frame rate from network: {:.{}f} FPS\n"
                    "Decoding frame rate: {:.{}f} FPS\n"
                    "Rendering frame rate: {:.{}f} FPS\n",
                stats->video_decode_stats.current_host_fps, 2,
                stats->video_decode_stats.current_received_fps, 2,
                stats->video_decode_stats.current_decoded_fps, 2,
                stats->video_render_stats.rendered_fps, 2);

    statistics += fmt::format("Frames dropped by your network connection: {}\n"
                              "Average receive time: {:.{}f} | {:.{}f} ms\n"
                              "Average decode time: {:.{}f} | {:.{}f} ms\n"
                              "Average decoder delay: {:.{}f} | {:.{}f} ms\n"
                              "Average rendering time: {:.{}f} ms\n",
                              stats->video_decode_stats.network_dropped_frames,
                              stats->video_decode_stats.current_receive_time, 2,
                              stats->video_decode_stats.session_receive_time, 2,
                              stats->video_decode_stats.current_decoding_time, 2,
                              stats->video_decode_stats.session_decoding_time, 2,
                              stats->video_decode_stats.current_decoder_delay, 2,
                              stats->video_decode_stats.session_decoder_delay, 2,
                              stats->video_render_stats.rendering_time, 2);

    if (stats->video_render_stats.gpu_timed_frames > 0) {
        statistics += fmt::format("Average GPU render time: {:.{}f} ms\n",
                                  stats->video_render_stats.gpu_rendering_time, 2);
    }

    if (stats->video_render_stats.post_processed_frames > 0) {
        statistics += fmt::format(
            "Average post-processing pass time: {:.{}f} ms (D:{:.{}f} | U:{:.{}f} | S:{:.{}f})\n",
            /* "Post-processed frames: {} / {}\n", */
            stats->video_render_stats.post_processing_time, 2,
            stats->video_render_stats.dithering_time, 2,
            stats->video_render_stats.upscaling_time, 2,
            stats->video_render_stats.sharpening_time, 2
            /*stats->video_render_stats.post_processed_frames, */
            /*stats->video_render_stats.rendered_frames*/);
    }

#ifdef __SWITCH__
    statistics += fmt::format("Vsync latch delay | misses: {:.2f} ms | {}\n",
                              latchDelay.count() / 1e6, latchMissStat);
#endif
    statistics += fmt::format("Frames queue underflows | skipped: {} | {}\n"
                              "Queue empty | startup holds: {} | {}\n"
                              "Queue overflow | paced skips: {} | {}\n"
                              "Scheduled frame holds: {}\n"
                              "Frames presented by local clock: {}\n"
                              "Playout resyncs | estimated source: {} | {:.2f} FPS\n"
                              "Max pushes between draws: {}\n"
                              "Frames queue depth | target | capacity: {} | {} | {}",
                              AVFrameHolder::instance().getFakeFrameStat(),
                              AVFrameHolder::instance().getFrameDropStat(),
                              AVFrameHolder::instance().getFrameQueueEmptyStat(),
                              AVFrameHolder::instance().getFrameQueueRebufferHoldStat(),
                              AVFrameHolder::instance().getFrameQueueOverflowDropStat(),
                              AVFrameHolder::instance().getFrameQueuePacingSkipStat(),
                              AVFrameHolder::instance().getFrameQueueScheduledHoldStat(),
                              AVFrameHolder::instance().getFrameQueueLocalClockPacedFrameStat(),
                              AVFrameHolder::instance().getFrameQueuePlayoutResyncStat(),
                              AVFrameHolder::instance().getFrameQueueEstimatedSourceFps(),
                              AVFrameHolder::instance().getFrameQueueMaxPushBurstStat(),
                              AVFrameHolder::instance().getFrameQueueSize(),
                              AVFrameHolder::instance().getFrameQueueTargetDepth(),
                              AVFrameHolder::instance().getFrameQueueCapacity());
    return statistics;
}

void StreamingView::addKeyboard() {
    if (keyboard)
        return;

    keyboard = new KeyboardView(false);
    keyboardHolder->addView(keyboard);
}

void StreamingView::removeKeyboard() {
    if (!keyboard)
        return;

    keyboard->removeFromSuperView();
    keyboard = nullptr;
    Application::giveFocus(this);
}

void StreamingView::terminate(bool terminateApp) {
    if (terminated)
        return;
    terminated = true;

    session->stop(terminateApp);

    int controllersCount = Application::getPlatform()->getInputManager()->getControllersConnectedCount();
    for (int i = 0; i < controllersCount; i++)
        Application::getPlatform()->getInputManager()->sendRumble(i, 0, 0);

    bool hasOverlays =
        Application::getActivitiesStack().back() != this->getParentActivity();
    this->dismiss([this, hasOverlays] {
        if (hasOverlays)
            this->dismiss();
    });
}

void StreamingView::handleInput() {
    if (!this->focused) {
        MoonlightInputManager::instance().dropInput();
        return;
    }

    if (keyboard) {
        static KeyboardState oldKeyboardState;
        KeyboardState keyboardState = keyboard->getKeyboardState();

        for (int i = 0; i < _VK_KEY_MAX; i++) {
            if (keyboardState.keys[i] != oldKeyboardState.keys[i]) {
                oldKeyboardState.keys[i] = keyboardState.keys[i];
                LiSendKeyboardEvent(
                    keyboard->getKeyCode((KeyboardKeys)i),
                    keyboardState.keys[i] ? KEY_ACTION_DOWN : KEY_ACTION_UP, 0);
            }
        }

        // Drop input if keyboard overlay presented
//        MoonlightInputManager::instance().dropInput();
    }
//    else {
    MoonlightInputManager::instance().handleInput(keyboard != nullptr);
//    }

    if (!Application::currentTouchState.empty()) {
        setBottomBarStatus("2");

        if (bottombarDelayTask != -1)
            cancelDelay(bottombarDelayTask);

        ASYNC_RETAIN
        bottombarDelayTask = delay(3000, [ASYNC_TOKEN]() {
            ASYNC_RELEASE
            setBottomBarStatus("1");
            bottombarDelayTask = -1;
        });
    }
}

#ifdef __SWITCH__
// Late latching: with double buffering the swapchain acquire in beginFrame()
// returns right after vsync, so draw() starts phase-locked to the display.
// Sleeping here lets frames decoded during the wait still make the next
// vsync. The wait adapts: a missed vsync (draw interval ~2 periods) backs it
// off and lowers a ceiling that only relaxes slowly, so probing rarely misses.
void StreamingView::latchBeforeVsync() {
    using namespace std::chrono;
    constexpr nanoseconds period(16666667);
    constexpr nanoseconds maxDelay = period - milliseconds(5);
    constexpr nanoseconds growStep = microseconds(50);
    constexpr nanoseconds ceilingRelaxStep = microseconds(5);

    const auto now = steady_clock::now();
    if (lastDrawStart.time_since_epoch().count() != 0) {
        const auto interval = now - lastDrawStart;
        if (interval > milliseconds(100)) {
            // Suspended or loading; not a deadline miss.
        } else if (interval > period * 3 / 2) {
            latchMissStat++;
            latchCeiling = std::max(nanoseconds::zero(), latchDelay - milliseconds(1));
            latchDelay = std::max(nanoseconds::zero(), latchDelay - milliseconds(2));
        } else {
            latchCeiling = std::min(latchCeiling + ceilingRelaxStep, maxDelay);
            latchDelay = std::min(latchDelay + growStep, latchCeiling);
        }
    }
    lastDrawStart = now;

    if (latchDelay > nanoseconds::zero())
        std::this_thread::sleep_for(latchDelay);
}
#endif

void StreamingView::handleOverlayCombo() {
    if (!this->focused)
        return;

    KeyComboOptions options = Settings::instance().overlay_options();

    static ControllerState controller;
    Application::getPlatform()->getInputManager()->updateUnifiedControllerState(
        &controller);

    static std::chrono::high_resolution_clock::time_point clock_counter;
    static bool buttonState = false;
    static bool used = false;

    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::high_resolution_clock::now() - clock_counter);

    bool buttonsPressed = true;
    for (auto button : options.buttons) {
        buttonsPressed &= controller.buttons[button];
    }

    if (!buttonState && buttonsPressed) {
        buttonState = true;
        clock_counter = std::chrono::high_resolution_clock::now();
    } else if (buttonState && !buttonsPressed) {
        buttonState = false;
        used = false;
    } else if (buttonState && duration.count() >= options.holdTime && !used) {
        used = true;

        auto overlay = new IngameOverlay(this);
        Application::pushActivity(new Activity(overlay));
    }

#ifdef PLATFORM_SWITCH
    static bool oldSystemButtonOverlayPressed = false;
    bool systemButtonOverlayPressed = false;
    if (Settings::instance().get_overlay_system_button() == ButtonOverrideType::HOME)
        systemButtonOverlayPressed |= ((SwitchInputManager*) Application::getPlatform()->getInputManager())->isHomeButtonPressed();

    if (Settings::instance().get_overlay_system_button() == ButtonOverrideType::SCREENSHOT)
        systemButtonOverlayPressed |= ((SwitchInputManager*) Application::getPlatform()->getInputManager())->isScreenshotButtonPressed();

    if (oldSystemButtonOverlayPressed != systemButtonOverlayPressed) {
        oldSystemButtonOverlayPressed = systemButtonOverlayPressed;
        if (systemButtonOverlayPressed) {
            auto overlay = new IngameOverlay(this);
            Application::pushActivity(new Activity(overlay));
        }
    }
#endif
}

void StreamingView::handleMouseInputCombo() {
    if (!this->focused)
        return;

    KeyComboOptions options = Settings::instance().mouse_input_options();
    if (options.buttons.empty())
        return;

    static ControllerState controller;
    Application::getPlatform()->getInputManager()->updateUnifiedControllerState(
        &controller);

    static std::chrono::high_resolution_clock::time_point clock_counter;
    static bool buttonState = false;
    static bool used = false;

    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::high_resolution_clock::now() - clock_counter);

    bool buttonsPressed = true;
    for (auto button : options.buttons) {
        buttonsPressed &= controller.buttons[button];
    }

    if (!buttonState && buttonsPressed) {
        buttonState = true;
        clock_counter = std::chrono::high_resolution_clock::now();
    } else if (buttonState && !buttonsPressed) {
        buttonState = false;
        used = false;
    } else if (buttonState && duration.count() >= options.holdTime && !used) {
        used = true;

        auto overlay = new StreamingInputOverlay(this);
        Application::pushActivity(new Activity(overlay));
    }
}

void StreamingView::onLayout() {
    Box::onLayout();
    if (loader)
        loader->layout();

    if (keyboardHolder) {
        keyboardHolder->setWidth(getWidth());
        keyboardHolder->setHeight(getHeight());
    }
}

StreamingView::~StreamingView() {
#ifdef PLATFORM_TVOS
    updatePreferredDisplayMode(false);
#endif
    
    MoonlightInputManager::instance().stopControllerPolling();
    Application::getPlatform()->disableScreenDimming(false);
    Application::getWindowFocusChangedEvent()->unsubscribe(
        windowFocusSubscription);
    Application::getPlatform()
        ->getInputManager()
        ->getKeyboardKeyStateChanged()
        ->unsubscribe(keysSubscription);
    session->stop(false);
    delete session;
}
