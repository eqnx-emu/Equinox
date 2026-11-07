//
//  settings_tab.hpp
//  Moonlight
//
//  Created by XITRIX on 26.05.2021.
//

#pragma once

#include <borealis.hpp>
#include <string>
#include <vector>

#include "views/boolean_slider_cell.hpp"

class SettingsTab : public brls::Box {
  public:
    // group: id of a settings group ("video", "audio", ...) to show alone;
    // B then leaves the screen instead of going back to the menu.
    explicit SettingsTab(const std::string& group = "");
    ~SettingsTab();

    void updateBitrateHints();

    BRLS_BIND(brls::SelectorCell, resolution, "resolution");
    BRLS_BIND(brls::SelectorCell, resolutionScale, "resolution_scale");
    BRLS_BIND(brls::Header, imageAdjustmentsHeader, "image_adjustments_header");
    BRLS_BIND(BooleanSliderCell, dithering, "dithering");
    BRLS_BIND(brls::BooleanCell, upscaling, "upscaling");
    BRLS_BIND(brls::SelectorCell, upscalingMode, "upscaling_mode");
    BRLS_BIND(BooleanSliderCell, rcas, "rcas");
    BRLS_BIND(brls::SelectorCell, fps, "fps");
    BRLS_BIND(brls::SelectorCell, framePacing, "frame_pacing");
    BRLS_BIND(brls::SelectorCell, codec, "codec");
    BRLS_BIND(brls::BooleanCell, requestHdr, "request_hdr");
    BRLS_BIND(brls::SelectorCell, decoder, "decoder");
    BRLS_BIND(brls::BooleanCell, hwDecoding, "use_hw_decoding");
    BRLS_BIND(brls::Header, header, "header");
    BRLS_BIND(brls::Slider, slider, "slider");
    BRLS_BIND(brls::Header, handheldHeader, "handheld_header");
    BRLS_BIND(brls::BooleanCell, modeProfiles, "mode_profiles");
    BRLS_BIND(brls::SelectorCell, handheldResolution, "handheld_resolution");
    BRLS_BIND(brls::Header, handheldBitrateHeader, "handheld_bitrate_header");
    BRLS_BIND(brls::Slider, handheldBitrateSlider, "handheld_bitrate_slider");
    BRLS_BIND(brls::SelectorCell, audioBackend, "audio_backend");
    BRLS_BIND(brls::BooleanCell, optimal, "optimal");
    BRLS_BIND(brls::BooleanCell, pcAudio, "pcAudio");
    BRLS_BIND(brls::BooleanCell, swapUi, "swap_ui");
    BRLS_BIND(brls::DetailCell, swapGame, "swap_game");
    BRLS_BIND(brls::DetailCell, deadzoneStickLeft, "dead_zone_stick_left");
    BRLS_BIND(brls::DetailCell, deadzoneStickRight, "dead_zone_stick_right");
    BRLS_BIND(brls::Header, rumbleForceHeader, "rumble_slider_header");
    BRLS_BIND(brls::Slider, rumbleForceSlider, "rumble_slider");
    BRLS_BIND(brls::BooleanCell, swapStickToDpad, "swap_stick_to_dpad");
    BRLS_BIND(brls::DetailCell, guideKeyButtons, "guide_key_buttons");
    BRLS_BIND(brls::SelectorCell, guideBySystemButton, "guide_by_system_button");
    BRLS_BIND(brls::SelectorCell, overlayTime, "overlay_time");
    BRLS_BIND(brls::DetailCell, overlayButtons, "overlay_buttons");
    BRLS_BIND(brls::SelectorCell, overlayBySystemButton, "overlay_by_system_button");
    BRLS_BIND(brls::SelectorCell, mouseInputTime, "mouse_input_time");
    BRLS_BIND(brls::DetailCell, mouseInputButtons, "mouse_input_buttons");
    BRLS_BIND(brls::SelectorCell, keyboardType, "keyboard_type");
    BRLS_BIND(brls::SelectorCell, keyboardFingers, "keyboard_fingers");
    BRLS_BIND(brls::BooleanCell, volumeAmplification, "volume_amplification");
    BRLS_BIND(brls::BooleanCell, touchscreenMouseMode, "touchscreen_mouse_mode");
    BRLS_BIND(brls::BooleanCell, swapMouseKeys, "swap_mouse_keys");
    BRLS_BIND(brls::BooleanCell, swapMouseScroll, "swap_mouse_scroll");
    BRLS_BIND(brls::Header, mouseSpeedHeader, "mouse_speed_header");
    BRLS_BIND(brls::Slider, mouseSpeedSlider, "mouse_speed_slider");
    BRLS_BIND(brls::BooleanCell, showMetrics, "show_metrics");
    BRLS_BIND(brls::Box, metricsOptions, "metrics_options");
    BRLS_BIND(brls::BooleanCell, metricFps, "metric_fps");
    BRLS_BIND(brls::BooleanCell, metricLatency, "metric_latency");
    BRLS_BIND(brls::BooleanCell, metricBitrate, "metric_bitrate");
    BRLS_BIND(brls::BooleanCell, metricLoss, "metric_loss");
    BRLS_BIND(brls::SelectorCell, metricsPosition, "metrics_position");
    BRLS_BIND(brls::SelectorCell, metricsColor, "metrics_color");
    BRLS_BIND(brls::SelectorCell, metricsBackground, "metrics_background");
    BRLS_BIND(brls::BooleanCell, writeLog, "writeLog");
    BRLS_BIND(brls::DetailCell, checkUpdate, "check_update");

    BRLS_BIND(brls::ScrollingFrame, scroll, "scroll");
    BRLS_BIND(brls::Label, pageTitle, "page_title");
    BRLS_BIND(brls::Box, menu, "menu");

    static brls::View* create();

  private:
    // Settings are split into groups: the tab opens on a menu with one entry
    // per group, and B goes back to it from inside a group.
    struct Group {
        brls::Box* entry;
        brls::Box* content;
        std::string title;
        std::string id;
    };
    std::vector<Group> groups;
    bool directGroup = false;

    void setupGroups();
    // Info button on the technical video options.
    void setupInfo();
    void openGroup(size_t index);
    void closeGroup(size_t index);

    void setupButtonsSelectorCell(brls::DetailCell* cell,
                                  std::vector<brls::ControllerButton> buttons);
    std::string getTextFromButtons(std::vector<brls::ControllerButton> buttons);
    NVGcolor getColorFromButtons(std::vector<brls::ControllerButton> buttons);
    void updateDeadZoneItems();
};
