//
//  settings_tab.cpp
//  Moonlight
//
//  Created by XITRIX on 26.05.2021.
//

#ifdef PLATFORM_SWITCH
#include <borealis/platforms/switch/switch_input.hpp>
#endif

#include "settings_tab.hpp"
#include "Updater.hpp"
#include "Settings.hpp"
#include "helper.hpp"
#include "button_selecting_dialog.hpp"
#include "mapping_layout_editor.hpp"
#include "main_tabs_view.hpp"
#include "grid_view.hpp"
#include "pressable_box.hpp"
#include "bold_label.hpp"
#include "UpscalingSupport.hpp"
#include "StreamCapacity.hpp"
#include <fmt/core.h>
#include <cmath>
#include <iomanip>
#include <algorithm>
#include <sstream>

#define SET_SETTING(n, func)                                                   \
    case n:                                                                    \
        Settings::instance().func;                                             \
        break;

#define GET_SETTINGS(combo_box, n, i)                                          \
    case n:                                                                    \
        combo_box->setSelection(i);                                            \
        break;

#define DEFAULT                                                                \
    default:                                                                   \
        break;

using namespace brls::literals;


namespace {
void updateStrengthControl(BooleanSliderCell* cell,
                           bool enabled, const std::string& detailText) {
    cell->setSliderVisibility(enabled ? brls::Visibility::VISIBLE
                                      : brls::Visibility::GONE);
    cell->setValueText(enabled ? detailText : "");
}

float sliderProgressToDitheringStrength(float progress) {
    return static_cast<float>(int(progress * 9.0f + 0.5f) + 1);
}

float ditheringStrengthToSliderProgress(float strength) {
    if (strength < 1.0f)
        strength = 1.0f;
    else if (strength > 10.0f)
        strength = 10.0f;

    return (strength - 1.0f) / 9.0f;
}

std::string getDitheringStrengthText(float strength) {
    return std::to_string(int(strength));
}

std::string getRcasStrengthText(float strength) {
    return std::to_string(int(strength * 100.0f)) + "%";
}

std::string formatMbps(int kbps) {
    std::stringstream stream;
    stream << std::fixed << std::setprecision(1) << kbps / 1000.0f;
    return stream.str();
}

// Same native-size source as MoonlightSession::start.
int minRecommendedFor(int resolution) {
    int nativeW = 0, nativeH = 0;
    if (resolution == -1) {
        nativeH = brls::Application::windowHeight;
        nativeW = brls::Application::windowWidth;
    }
    auto [w, h] = stream_size_for(resolution, nativeW, nativeH,
                                  Settings::instance().native_resolution_scale());
    return min_recommended_bitrate_kbps(w, h, Settings::instance().fps(),
                                        Settings::instance().video_codec());
}

void applyBitrateHint(brls::Header* header, int currentKbps, int minKbps) {
    const bool below = currentKbps < minKbps;
    std::string text = brls::getStr("settings/bitrate_min_hint", formatMbps(currentKbps),
                      formatMbps(minKbps));
    if (below)
        text += " · " + "settings/bitrate_below_min"_i18n;
    header->setSubtitle(text);

    // Header exposes no subtitle color API; reach its inflated label by id.
    if (auto* label = dynamic_cast<brls::Label*>(
            header->getView("brls/header/subtitle"))) {
        label->setTextColor(below ? nvgRGB(255, 140, 0)
                                  : brls::Application::getTheme()["brls/header/subtitle"]);
    }
}
}

#if defined(__SWITCH__)
std::vector<std::string> audio_backends {
    "Audren",
};
#elif defined(__SDL2__) || defined(__SDL3__)
std::vector<std::string> audio_backends {
#if defined(__SDL3__)
    "SDL3",
#else
    "SDL2",
#endif
};
#else
std::vector<std::string> audio_backends;
#endif


void SettingsTab::updateBitrateHints() {
    applyBitrateHint(header, Settings::instance().bitrate(),
                     minRecommendedFor(Settings::instance().resolution()));
#if defined(PLATFORM_SWITCH)
    applyBitrateHint(handheldBitrateHeader, Settings::instance().handheld_bitrate(),
                     minRecommendedFor(Settings::instance().handheld_resolution()));
#endif
}

SettingsTab::SettingsTab() {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/settings.xml");

    std::vector<std::string> resolutions = {
        "settings/resolution_native"_i18n, "360p", "480p", "540p", "720p", "1080p",
        "1440p",
    };
    resolution->setText("settings/resolution"_i18n);
    resolution->setData(resolutions);
    switch (Settings::instance().resolution()) {
        GET_SETTINGS(resolution, -1, 0);
        GET_SETTINGS(resolution, 360, 1);
        GET_SETTINGS(resolution, 480, 2);
        GET_SETTINGS(resolution, 540, 3);
        GET_SETTINGS(resolution, 720, 4);
        GET_SETTINGS(resolution, 1080, 5);
        GET_SETTINGS(resolution, 1440, 6);
        DEFAULT;
    }

    std::vector<std::string> nativeResolutionScales = {
        "0.5x", "0.75x", "1.0x",
        "2.0x",
    };
    resolutionScale->setText("settings/resolution_scale"_i18n);
    resolutionScale->setData(nativeResolutionScales);
    switch (Settings::instance().native_resolution_scale()) {
        GET_SETTINGS(resolutionScale, 50, 0);
        GET_SETTINGS(resolutionScale, 75, 1);
        GET_SETTINGS(resolutionScale, 100, 2);
        GET_SETTINGS(resolutionScale, 200, 3);
        default:
            resolutionScale->setSelection(2);
            break;
    }
    resolutionScale->getEvent()->subscribe([this](int selected) {
        switch (selected) {
            SET_SETTING(0, set_native_resolution_scale(50));
            SET_SETTING(1, set_native_resolution_scale(75));
            SET_SETTING(2, set_native_resolution_scale(100));
            SET_SETTING(3, set_native_resolution_scale(200));
            DEFAULT;
        }
        updateBitrateHints();
    });

#ifdef SUPPORT_UPSCALING
    if (!isVideoUpscalingSupported()) {
        imageAdjustmentsHeader->removeFromSuperView(true);
        dithering->removeFromSuperView(true);
        upscaling->removeFromSuperView(true);
        upscalingMode->removeFromSuperView(true);
        rcas->removeFromSuperView(true);
    } else {
        auto updateDitheringControls = [this](bool enabled) {
            updateStrengthControl(dithering, enabled,
                                  getDitheringStrengthText(
                                      Settings::instance().dithering_strength()));
        };
        auto updateRcasControls = [this](bool enabled) {
            updateStrengthControl(rcas, enabled,
                                  getRcasStrengthText(
                                      Settings::instance().rcas_strength()));
        };

        dithering->init("settings/dithering"_i18n, Settings::instance().dithering(),
                        [updateDitheringControls](bool value) {
                            Settings::instance().set_dithering(value);
                            updateDitheringControls(value);
                        });
        const float ditheringStrength = Settings::instance().dithering_strength();
        dithering->getProgressEvent()->subscribe([this](float value) {
            const float strength = sliderProgressToDitheringStrength(value);
            Settings::instance().set_dithering_strength(strength);
            updateStrengthControl(dithering,
                                  Settings::instance().dithering(),
                                  getDitheringStrengthText(strength));
        });
        dithering->setProgress(
            ditheringStrengthToSliderProgress(ditheringStrength));
        updateDitheringControls(Settings::instance().dithering());

        upscalingMode->removeFromSuperView(true);
        upscaling->init("settings/upscaling"_i18n, Settings::instance().upscaling(),
                        [](bool value) { Settings::instance().set_upscaling(value); });
        rcas->init("settings/rcas_sharpening"_i18n, Settings::instance().rcas(),
                   [updateRcasControls](bool value) {
                       Settings::instance().set_rcas(value);
                       updateRcasControls(value);
                   });

        const float rcasStrength = Settings::instance().rcas_strength();
        rcas->getProgressEvent()->subscribe([this](float value) {
            Settings::instance().set_rcas_strength(value);
            updateStrengthControl(rcas,
                                  Settings::instance().rcas(),
                                  getRcasStrengthText(value));
        });
        rcas->setProgress(rcasStrength);
        updateRcasControls(Settings::instance().rcas());
    }
#else
    imageAdjustmentsHeader->removeFromSuperView(true);
    dithering->removeFromSuperView(true);
    upscaling->removeFromSuperView(true);
    upscalingMode->removeFromSuperView(true);
    rcas->removeFromSuperView(true);
#endif

    auto updateNativeResolutionScaleVisibility = [this]() {
        resolutionScale->setVisibility(
            resolution->getSelection() == 0 ? brls::Visibility::VISIBLE
                                            : brls::Visibility::GONE);
    };
    updateNativeResolutionScaleVisibility();

    resolution->getEvent()->subscribe([this, updateNativeResolutionScaleVisibility](int selected) {
        switch (selected) {
            SET_SETTING(0, set_resolution(-1));
            SET_SETTING(1, set_resolution(360));
            SET_SETTING(2, set_resolution(480));
            SET_SETTING(3, set_resolution(540));
            SET_SETTING(4, set_resolution(720));
            SET_SETTING(5, set_resolution(1080));
            SET_SETTING(6, set_resolution(1440));
            DEFAULT;
        }
        updateNativeResolutionScaleVisibility();
        updateBitrateHints();
    });

    std::vector<std::string> fpss = {
        "30",
        "40",
        "60",
        "120",
    };
    fps->setText("settings/fps"_i18n);
    fps->setData(fpss);
    switch (Settings::instance().fps()) {
        GET_SETTINGS(fps, 30, 0);
        GET_SETTINGS(fps, 40, 1);
        GET_SETTINGS(fps, 60, 2);
        GET_SETTINGS(fps, 120, 3);
        DEFAULT;
    }
    fps->getEvent()->subscribe([this](int selected) {
        switch (selected) {
            SET_SETTING(0, set_fps(30));
            SET_SETTING(1, set_fps(40));
            SET_SETTING(2, set_fps(60));
            SET_SETTING(3, set_fps(120));
            DEFAULT;
        }
        updateBitrateHints();
    });

    // Maps to frames_queue_size: 1 = no jitter reserve, 2 = one frame,
    // 3+ = two frames.
    framePacing->setText("settings/frame_pacing"_i18n);
    framePacing->setData({"settings/frame_pacing_latency"_i18n,
                          "settings/frame_pacing_balanced"_i18n,
                          "settings/frame_pacing_smooth"_i18n});
    framePacing->setSelection(
        std::clamp(Settings::instance().frames_queue_size(), 1, 3) - 1);
    framePacing->getEvent()->subscribe([](int selected) {
        Settings::instance().set_frames_queue_size(selected + 1);
    });

    std::vector<std::string> decoders = {"settings/zero_threads"_i18n, "2", "3",
                                         "4"};
    decoder->setText("settings/decoder_threads"_i18n);
    decoder->setData(decoders);
    switch (Settings::instance().decoder_threads()) {
        GET_SETTINGS(decoder, 0, 0);
        GET_SETTINGS(decoder, 2, 1);
        GET_SETTINGS(decoder, 3, 2);
        GET_SETTINGS(decoder, 4, 3);
        DEFAULT;
    }
    decoder->getEvent()->subscribe([](int selected) {
        switch (selected) {
            SET_SETTING(0, set_decoder_threads(0));
            SET_SETTING(1, set_decoder_threads(2));
            SET_SETTING(2, set_decoder_threads(3));
            SET_SETTING(3, set_decoder_threads(4));
            DEFAULT;
        }
    });

    std::vector<VideoCodec> supportedCodecs = {
        H264,
        H265,
    };

    std::vector<std::string> supportedCodecNames;
    for (int i = 0; i < supportedCodecs.size(); i++) {
        supportedCodecNames.push_back(getVideoCodecName(supportedCodecs[i]));
    }

    auto it = find(supportedCodecs.begin(), supportedCodecs.end(), Settings::instance().video_codec());

    int selected = 0;
    if (it != supportedCodecs.end()) {
        int index = it - supportedCodecs.begin();
        selected = index;
    }

    codec->init("settings/video_codec"_i18n, supportedCodecNames,
                selected, [this, supportedCodecs](int selected) {
                    Settings::instance().set_video_codec(supportedCodecs[selected]);
                    updateBitrateHints();
                });

    requestHdr->init("settings/request_hdr"_i18n, Settings::instance().request_hdr(),
                     [](bool value) { Settings::instance().set_request_hdr(value); });

    requestHdr->removeFromSuperView(true);

    hwDecoding->init("settings/use_hw_decoding"_i18n, Settings::instance().use_hw_decoding(),
                     [](bool value) { Settings::instance().set_use_hw_decoding(value); });

    hwDecoding->setEnabled(false);

#if   defined(PLATFORM_SWITCH)
    const float mbpsMaxLimit = 100000;
#else
    const float mbpsMaxLimit = 150000;
#endif

    const float limitOffset = 500;
    const float limit = mbpsMaxLimit - limitOffset;

    float progress = (Settings::instance().bitrate() - limitOffset) / limit;
    slider->getProgressEvent()->subscribe([this, limitOffset, limit](float progress) {
        int bitrate = progress * limit + limitOffset;
        Settings::instance().set_bitrate(bitrate);
        updateBitrateHints();
    });
    slider->setProgress(progress);
    updateBitrateHints();

#if defined(PLATFORM_SWITCH)
    // Handheld profile: when enabled, the values above apply while docked and
    // these apply while undocked. The mode is sampled when a stream starts.
    const std::vector<int> handheldResolutionValues = {480, 540, 720, 1080};
    std::vector<std::string> handheldResolutionNames;
    for (int value : handheldResolutionValues)
        handheldResolutionNames.push_back(std::to_string(value) + "p");

    auto handheldIt = std::find(handheldResolutionValues.begin(),
                                handheldResolutionValues.end(),
                                Settings::instance().handheld_resolution());
    int handheldSelection =
        handheldIt != handheldResolutionValues.end()
            ? (int) (handheldIt - handheldResolutionValues.begin())
            : 2;

    handheldResolution->init(
        "settings/handheld_resolution"_i18n, handheldResolutionNames,
        handheldSelection, [this, handheldResolutionValues](int selected) {
            Settings::instance().set_handheld_resolution(
                handheldResolutionValues[selected]);
            updateBitrateHints();
        });

    auto updateHandheldControls = [this](bool enabled) {
        handheldResolution->setVisibility(enabled ? brls::Visibility::VISIBLE
                                                  : brls::Visibility::GONE);
        handheldBitrateHeader->setVisibility(
            enabled ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        handheldBitrateSlider->setVisibility(
            enabled ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    };

    modeProfiles->init("settings/mode_profiles"_i18n,
                       Settings::instance().mode_profiles(),
                       [this, updateHandheldControls](bool value) {
                           Settings::instance().set_mode_profiles(value);
                           updateHandheldControls(value);
                           updateBitrateHints();
                       });
    updateHandheldControls(Settings::instance().mode_profiles());

    handheldBitrateSlider->getProgressEvent()->subscribe(
        [this, limitOffset, limit](float progress) {
            int bitrate = progress * limit + limitOffset;
            Settings::instance().set_handheld_bitrate(bitrate);
            updateBitrateHints();
        });
    handheldBitrateSlider->setProgress(
        (Settings::instance().handheld_bitrate() - limitOffset) / limit);
#else
    handheldHeader->removeFromSuperView(true);
    modeProfiles->removeFromSuperView(true);
    handheldResolution->removeFromSuperView(true);
    handheldBitrateHeader->removeFromSuperView(true);
    handheldBitrateSlider->removeFromSuperView(true);
#endif

#if defined(__SWITCH__)
    audioBackend->init("settings/audio_backend"_i18n, audio_backends, 0,
                       [](int) { Settings::instance().set_audio_backend(AUDREN); });
#elif defined(__SDL2__) || defined(__SDL3__)
    audioBackend->init("settings/audio_backend"_i18n, audio_backends, Settings::instance().audio_backend(),
                       [](int selected) { Settings::instance().set_audio_backend((AudioBackend)selected); });
#else
    audioBackend->removeFromSuperView(true);
#endif

    optimal->init("settings/usops"_i18n, Settings::instance().sops(),
                  [](bool value) { Settings::instance().set_sops(value); });

    pcAudio->init(
        "settings/paop"_i18n, Settings::instance().play_audio(),
        [](bool value) { Settings::instance().set_play_audio(value); });

    swapUi->init("settings/swap_ui"_i18n,
                 Settings::instance().swap_game_face_buttons(),
                 [](bool value) {
                     Settings::instance().set_swap_game_face_buttons(value);
                 });

    std::vector<std::string> layouts;
    for (KeyMappingLayout layout : *Settings::instance().get_mapping_laouts())
        layouts.push_back(layout.title);
    layouts.push_back("settings/keys_mapping_create_new"_i18n);

    swapGame->setText("settings/keys_mapping_title"_i18n);
    swapGame->setDetailTextColor(
        Application::getTheme()["brls/list/listItem_value_color"]);
    swapGame->setDetailText(
        layouts[Settings::instance().get_current_mapping_layout()]);

    swapGame->registerClickAction([this](View* view) {
        auto layouts = *Settings::instance().get_mapping_laouts();
        int current = Settings::instance().get_current_mapping_layout();

        std::vector<std::string> layoutTexts;
        for (KeyMappingLayout layout : layouts)
            layoutTexts.push_back(layout.title);
        layoutTexts.push_back("settings/keys_mapping_create_new"_i18n);

        Dropdown* dropdown = new Dropdown(
            swapGame->title->getFullText(), layoutTexts,
            [this, layoutTexts](int selected) {
                if (selected <
                    Settings::instance().get_mapping_laouts()->size()) {
                    Settings::instance().set_current_mapping_layout(selected);
                    swapGame->setDetailText(layoutTexts[selected]);
                }
            },
            Settings::instance().get_current_mapping_layout(),
            [this](int selected) {
                if (Settings::instance().get_mapping_laouts()->size() ==
                    selected) {
                    KeyMappingLayout layout;
                    layout.title = "settings/keys_mapping_new_title"_i18n;
                    layout.editable = true;
                    Settings::instance().get_mapping_laouts()->push_back(
                        layout);
                    Settings::instance().set_current_mapping_layout(selected);
                    swapGame->setDetailText(layout.title);

                    // Show layout editor View
                    MappingLayoutEditor* editor =
                        new MappingLayoutEditor(selected, [this] {
                            auto currentLayout =
                                Settings::instance().get_mapping_laouts()->at(
                                    Settings::instance()
                                        .get_current_mapping_layout());
                            this->swapGame->setDetailText(currentLayout.title);
                        });
                    MainTabs::presentWithHeader(this, editor);
                }
            });

        dropdown->registerAction(
            "common/edit"_i18n, BUTTON_Y, [this, dropdown](View* view) {
                Application::popActivity(
                    brls::TransitionAnimation::FADE, [this, dropdown]() {
                        RecyclerCell* cell = dynamic_cast<RecyclerCell*>(
                            dropdown->getDefaultFocus());
                        if (cell) {
                            // Show layout editor View
                            int index = cell->getIndexPath().row;
                            MappingLayoutEditor* editor =
                                new MappingLayoutEditor(index, [this] {
                                    auto currentLayout =
                                        Settings::instance()
                                            .get_mapping_laouts()
                                            ->at(
                                                Settings::instance()
                                                    .get_current_mapping_layout());
                                    this->swapGame->setDetailText(
                                        currentLayout.title);
                                });
                            MainTabs::presentWithHeader(this, editor);
                        }
                    });
                return true;
            });
        dropdown->setActionAvailable(BUTTON_Y, current < layouts.size() &&
                                                   layouts[current].editable);

        dropdown->getCellFocusDidChangeEvent()->subscribe(
            [dropdown](RecyclerCell* cell) {
                auto layouts = Settings::instance().get_mapping_laouts();
                int index = cell->getIndexPath().row;
                int layoutsCount =
                    (int)Settings::instance().get_mapping_laouts()->size();
                dropdown->setActionAvailable(BUTTON_Y,
                                             index < layoutsCount &&
                                                 layouts->at(index).editable);
            });

        Application::pushActivity(new Activity(dropdown));
        return true;
    });


    deadzoneStickLeft->setText("settings/deadzone/stick_left"_i18n);
    deadzoneStickRight->setText("settings/deadzone/stick_right"_i18n);

    updateDeadZoneItems();

    deadzoneStickLeft->registerClickAction([this](View* view) {
        int currentValue = int(Settings::instance().get_deadzone_stick_left() * 100);
        bool res = Application::getImeManager()->openForNumber([&](long number) {
                                                        Settings::instance().set_deadzone_stick_left(float(number) / 100.f);
                                                        this->updateDeadZoneItems();
                                                    },
                                                    "settings/deadzone/stick_left"_i18n, "settings/deadzone/input_hint"_i18n, 2,
                                                    currentValue > 0 ? std::to_string(currentValue) : "", "",
                                                    "", 0);

        if (!res) {
            Settings::instance().set_deadzone_stick_left(0);
            this->updateDeadZoneItems();
        }

        return true;
    });

    deadzoneStickRight->registerClickAction([this](View* view) {
        int currentValue = int(Settings::instance().get_deadzone_stick_right() * 100);
        bool res = Application::getImeManager()->openForNumber([&](long number) {
                                                        Settings::instance().set_deadzone_stick_right(float(number) / 100.f);
                                                        this->updateDeadZoneItems();
                                                    },
                                                    "settings/deadzone/stick_right"_i18n, "settings/deadzone/input_hint"_i18n, 2,
                                                    currentValue > 0 ? std::to_string(currentValue) : "", "",
                                                    "", 0);

        if (!res) {
            Settings::instance().set_deadzone_stick_right(0);
            this->updateDeadZoneItems();
        }

        return true;
    });

    float rumbleForceProgress = Settings::instance().get_rumble_force();
    rumbleForceSlider->getProgressEvent()->subscribe([this](float value) {
        std::stringstream stream;
        stream << std::fixed << std::setprecision(1) << int(value * 100);
        rumbleForceHeader->setSubtitle(stream.str() + "%");
        Settings::instance().set_rumble_force(value);
    });
    rumbleForceSlider->setProgress(rumbleForceProgress);

    swapStickToDpad->init("settings/swap_stick_to_dpad"_i18n, Settings::instance().swap_joycon_stick_to_dpad(),
                          [](bool value) { Settings::instance().set_swap_joycon_stick_to_dpad(value); });

    guideKeyButtons->setText("settings/guide_key_buttons"_i18n);
    setupButtonsSelectorCell(guideKeyButtons,
                             Settings::instance().guide_key_options().buttons);
    guideKeyButtons->registerClickAction([this](View* view) {
        ButtonSelectingDialog* dialog = ButtonSelectingDialog::create(
            "settings/guide_key_setup_message"_i18n, [this](auto buttons) {
                auto options = Settings::instance().guide_key_options();
                options.buttons = buttons;
                Settings::instance().set_guide_key_options(options);
                setupButtonsSelectorCell(guideKeyButtons, buttons);
            });

        dialog->open();
        return true;
    });

#ifndef PLATFORM_SWITCH
    guideBySystemButton->removeFromSuperView();
    overlayBySystemButton->removeFromSuperView();
#else
    guideBySystemButton->init(
        "settings/use_system_button"_i18n,
        {"hints/off"_i18n, "settings/buttons/screenshot"_i18n, "settings/buttons/home"_i18n},
        (int) Settings::instance().get_guide_system_button(), [this](int value) {
            if (value != 0 && Settings::instance().get_overlay_system_button() == (ButtonOverrideType) value) {
                brls::sync([this, value](){
                    showError("settings/system_button_duplication_error"_i18n, [](){});
                });
                guideBySystemButton->setSelection((int) Settings::instance().get_guide_system_button(), true);
                return;
            }

            Settings::instance().set_guide_system_button((ButtonOverrideType) value);

            auto color = Settings::instance().get_guide_system_button() == ButtonOverrideType::NONE ?
                Application::getTheme()["brls/text_disabled"] : Application::getTheme()["brls/accent"];
            guideBySystemButton->setDetailTextColor(color);
        });
    auto color = Settings::instance().get_guide_system_button() == ButtonOverrideType::NONE ?
         Application::getTheme()["brls/text_disabled"] : Application::getTheme()["brls/accent"];
    guideBySystemButton->setDetailTextColor(color);

    overlayBySystemButton->init(
        "settings/use_system_button"_i18n,
        {"hints/off"_i18n, "settings/buttons/screenshot"_i18n, "settings/buttons/home"_i18n},
        (int) Settings::instance().get_overlay_system_button(), [this](int value) {
            if (value != 0 && Settings::instance().get_guide_system_button() == (ButtonOverrideType) value) {
                brls::sync([this, value](){
                    showError("settings/system_button_duplication_error"_i18n, [](){});
                });
                overlayBySystemButton->setSelection((int) Settings::instance().get_overlay_system_button(), true);
                return;
            }

            Settings::instance().set_overlay_system_button((ButtonOverrideType) value);

            auto color = Settings::instance().get_overlay_system_button() == ButtonOverrideType::NONE ?
                Application::getTheme()["brls/text_disabled"] : Application::getTheme()["brls/accent"];
            overlayBySystemButton->setDetailTextColor(color);
        });
    color = Settings::instance().get_overlay_system_button() == ButtonOverrideType::NONE ?
         Application::getTheme()["brls/text_disabled"] : Application::getTheme()["brls/accent"];
    overlayBySystemButton->setDetailTextColor(color);
#endif

    overlayTime->init(
        "settings/overlay_time"_i18n,
        {"settings/overlay_zero_time"_i18n, "1", "2", "3", "4", "5"},
        Settings::instance().overlay_options().holdTime, [](int value) {
            auto options = Settings::instance().overlay_options();
            options.holdTime = value;
            Settings::instance().set_overlay_options(options);
        });

    overlayButtons->setText("settings/overlay_buttons"_i18n);
    setupButtonsSelectorCell(overlayButtons,
                             Settings::instance().overlay_options().buttons);
    overlayButtons->registerClickAction([this](View* view) {
        ButtonSelectingDialog* dialog = ButtonSelectingDialog::create(
            "settings/overlay_setup_message"_i18n, [this](auto buttons) {
                if (buttons.empty())
                    return;

                auto options = Settings::instance().overlay_options();
                options.buttons = buttons;
                Settings::instance().set_overlay_options(options);
                setupButtonsSelectorCell(overlayButtons, buttons);
            });

        dialog->open();
        return true;
    });

    mouseInputTime->init(
        "settings/overlay_time"_i18n,
        {"settings/overlay_zero_time"_i18n, "1", "2", "3", "4", "5"},
        Settings::instance().mouse_input_options().holdTime, [](int value) {
            auto options = Settings::instance().mouse_input_options();
            options.holdTime = value;
            Settings::instance().set_mouse_input_options(options);
        });

    mouseInputButtons->setText("settings/overlay_buttons"_i18n);
    setupButtonsSelectorCell(
        mouseInputButtons, Settings::instance().mouse_input_options().buttons);
    mouseInputButtons->registerClickAction([this](View* view) {
        ButtonSelectingDialog* dialog = ButtonSelectingDialog::create(
            "settings/mouse_input_setup_message"_i18n, [this](auto buttons) {
                auto options = Settings::instance().mouse_input_options();
                options.buttons = buttons;
                Settings::instance().set_mouse_input_options(options);
                setupButtonsSelectorCell(mouseInputButtons, buttons);
            });

        dialog->open();
        return true;
    });

    std::vector<std::string> keyboardTypes = {
        "settings/keyboard_compact"_i18n, "settings/keyboard_fullsized"_i18n};
    keyboardType->setText("settings/keyboard_type"_i18n);
    keyboardType->setData(keyboardTypes);
    switch (Settings::instance().get_keyboard_type()) {
        GET_SETTINGS(keyboardType, COMPACT, 0);
        GET_SETTINGS(keyboardType, FULLSIZED, 1);
        DEFAULT;
    }
    keyboardType->getEvent()->subscribe([](int selected) {
        switch (selected) {
            SET_SETTING(0, set_keyboard_type(COMPACT));
            SET_SETTING(1, set_keyboard_type(FULLSIZED));
            DEFAULT;
        }
    });

    std::vector<std::string> keyboardFingersOptions = {
        "3", "4", "5", "hints/off"_i18n};
    keyboardFingers->setText("settings/keyboard_fingers"_i18n);
    keyboardFingers->setData(keyboardFingersOptions);
    switch (Settings::instance().get_keyboard_fingers()) {
        GET_SETTINGS(keyboardFingers, 3, 0);
        GET_SETTINGS(keyboardFingers, 4, 1);
        GET_SETTINGS(keyboardFingers, 5, 2);
        GET_SETTINGS(keyboardFingers, -1, 3);
        DEFAULT;
    }
    keyboardFingers->getEvent()->subscribe([](int selected) {
        switch (selected) {
            SET_SETTING(0, set_keyboard_fingers(3));
            SET_SETTING(1, set_keyboard_fingers(4));
            SET_SETTING(2, set_keyboard_fingers(5));
            SET_SETTING(3, set_keyboard_fingers(-1));
            DEFAULT;
        }
    });

    volumeAmplification->init(
        "settings/volume_amplification"_i18n,
        Settings::instance().get_volume_amplification(), [this](auto value) {
            Settings::instance().set_volume_amplification(value);

            if (!value && Settings::instance().get_volume() > 100)
                Settings::instance().set_volume(100);
        });

    touchscreenMouseMode->init("settings/touchscreen_mouse_mode"_i18n,
                               Settings::instance().touchscreen_mouse_mode(),
                               [this](bool value) {
                                   Settings::instance().set_touchscreen_mouse_mode(value);
                               });

    swapMouseKeys->init("settings/swap_mouse_keys"_i18n,
                        Settings::instance().swap_mouse_keys(),
                        [this](bool value) {
                            Settings::instance().set_swap_mouse_keys(value);
                        });

    swapMouseScroll->init("settings/swap_mouse_scroll"_i18n,
                          Settings::instance().swap_mouse_scroll(),
                          [this](bool value) {
                              Settings::instance().set_swap_mouse_scroll(value);
                          });

    float mouseProgress =
        (Settings::instance().get_mouse_speed_multiplier() / 100.0f);
    mouseSpeedSlider->getProgressEvent()->subscribe([this](float value) {
        float multiplier = value * 1.5f + 0.5f;
        std::stringstream stream;
        stream << std::fixed << std::setprecision(1) << multiplier;
        mouseSpeedHeader->setSubtitle("x" + stream.str());
        Settings::instance().set_mouse_speed_multiplier(value * 100);
    });
    mouseSpeedSlider->setProgress(mouseProgress);

    writeLog->init("settings/debugging_view"_i18n,
                   Settings::instance().write_log(), [](bool value) {
                       Settings::instance().set_write_log(value);
                       brls::Application::enableDebuggingView(value);
                   });

    checkUpdate->setText("update/check_now"_i18n);
    checkUpdate->setDetailText(APP_VERSION);
    checkUpdate->registerClickAction([](View* view) {
        Updater::checkAndPrompt(false);
        return true;
    });

    setupGroups();
}

namespace {
// Menu card for a settings group: Material icon, name and what it covers.
PressableBox* makeGroupCard(const std::string& icon, const std::string& title,
                   const std::string& summary) {
    Theme theme = Application::getTheme();

    auto* card = new PressableBox(Axis::COLUMN);
    card->setWidth(200);
    card->setHeight(184);
    card->setPadding(18, 18, 18, 18);
    card->setCornerRadius(16);
    card->setHighlightCornerRadius(18);
    card->setHideHighlightBackground(true);
    card->setBackgroundColor(theme.getColor("equinox/card"));
    card->setFocusable(true);
    card->addGestureRecognizer(new TapGestureRecognizer(card));

    auto* badge = new Box(Axis::ROW);
    badge->setWidth(48);
    badge->setHeight(48);
    badge->setCornerRadius(24);
    badge->setJustifyContent(JustifyContent::CENTER);
    badge->setAlignItems(AlignItems::CENTER);
    badge->setBackgroundColor(theme.getColor("equinox/sky_night"));

    auto* glyph = new IconLabel();
    glyph->setFontSize(26);
    glyph->setTextColor(theme.getColor("equinox/sun"));
    glyph->setText(icon);
    badge->addView(glyph);
    card->addView(badge);

    auto* name = new BoldLabel();
    name->setFontSize(19);
    name->setMarginTop(14);
    name->setSingleLine(true);
    name->setText(title);
    card->addView(name);

    auto* text = new Label();
    text->setFontSize(14);
    text->setMarginTop(4);
    text->setTextColor(theme.getColor("equinox/muted"));
    text->setText(summary);
    card->addView(text);

    return card;
}
} // namespace

void SettingsTab::setupGroups() {
    // Material Icons code points.
    static const std::pair<const char*, const char*> ids[] = {
        {"video", "\ue04b"},     // videocam
        {"handheld", "\ue0d3"},  // stay_current_landscape
        {"image", "\ue429"},     // tune
        {"audio", "\ue050"},     // volume_up
        {"controls", "\ue338"},  // videogame_asset
        {"shortcuts", "\ue3e7"}, // flash_on
        {"input", "\ue312"},     // keyboard
        {"system", "\ue8b8"},    // settings
    };

    pageTitle->setText("tabs/settings"_i18n);

    auto* grid = new GridView(4);
    for (const auto& [id, icon] : ids) {
        const std::string key = std::string("settings/groups/") + id;
        auto* content = dynamic_cast<Box*>(getView(std::string("group_") + id));
        if (!content)
            continue;

        // Skip groups whose settings do not apply to this platform.
        bool hasVisibleSetting = false;
        for (View* child : content->getChildren()) {
            if (child->getVisibility() != Visibility::GONE &&
                !dynamic_cast<Header*>(child)) {
                hasVisibleSetting = true;
                break;
            }
        }
        if (!hasVisibleSetting)
            continue;

        const size_t index = groups.size();
        const std::string title = brls::getStr(key + "/title");
        PressableBox* card = makeGroupCard(icon, title, brls::getStr(key + "/summary"));
        groups.push_back({card, content, title});
        grid->addView(card);

        card->registerClickAction([this, card, index](View* view) {
            card->press([this, index] { openGroup(index); });
            return true;
        });
        content->registerAction("hints/back"_i18n, BUTTON_B,
                                [this, index](View* view) {
                                    closeGroup(index);
                                    return true;
                                });
    }
    menu->addView(grid);
}

void SettingsTab::openGroup(size_t index) {
    menu->setVisibility(Visibility::GONE);
    for (size_t i = 0; i < groups.size(); i++)
        groups[i].content->setVisibility(i == index ? Visibility::VISIBLE
                                                    : Visibility::GONE);
    pageTitle->setText(groups[index].title);
    scroll->setContentOffsetY(0, false);
    Application::giveFocus(groups[index].content);
}

void SettingsTab::closeGroup(size_t index) {
    groups[index].content->setVisibility(Visibility::GONE);
    menu->setVisibility(Visibility::VISIBLE);
    pageTitle->setText("tabs/settings"_i18n);
    scroll->setContentOffsetY(0, false);
    Application::giveFocus(groups[index].entry);
}

void SettingsTab::updateDeadZoneItems() {
    if (Settings::instance().get_deadzone_stick_left() > 0) {
        deadzoneStickLeft->setDetailTextColor(Application::getTheme()["brls/list/listItem_value_color"]);
        deadzoneStickLeft->setDetailText(fmt::format("{}%", int(Settings::instance().get_deadzone_stick_left() * 100.f)));
    } else {
        deadzoneStickLeft->setDetailTextColor(Application::getTheme()["brls/text_disabled"]);
        deadzoneStickLeft->setDetailText("hints/off"_i18n);
    }

    if (Settings::instance().get_deadzone_stick_right() > 0) {
        deadzoneStickRight->setDetailTextColor(Application::getTheme()["brls/list/listItem_value_color"]);
        deadzoneStickRight->setDetailText(fmt::format("{}%", int(Settings::instance().get_deadzone_stick_right() * 100)));
    } else {
        deadzoneStickRight->setDetailTextColor(Application::getTheme()["brls/text_disabled"]);
        deadzoneStickRight->setDetailText("hints/off"_i18n);
    }
}

void SettingsTab::setupButtonsSelectorCell(
    brls::DetailCell* cell, std::vector<ControllerButton> buttons) {
    cell->setDetailText(getTextFromButtons(buttons));
    cell->setDetailTextColor(getColorFromButtons(buttons));
}

std::string
SettingsTab::getTextFromButtons(std::vector<ControllerButton> buttons) {
    std::string buttonsText = "";
    if (buttons.size() > 0) {
        for (int i = 0; i < buttons.size(); i++) {
            buttonsText += brls::Hint::getKeyIcon(buttons[i], true);
            if (i < buttons.size() - 1)
                buttonsText += " + ";
        }
    } else {
        buttonsText = "hints/off"_i18n;
    }
    return buttonsText;
}

NVGcolor
SettingsTab::getColorFromButtons(std::vector<brls::ControllerButton> buttons) {
    Theme theme = Application::getTheme();
    return buttons.empty() ? theme["brls/text_disabled"]
                           : theme["brls/list/listItem_value_color"];
}

SettingsTab::~SettingsTab() { Settings::instance().save(); }

brls::View* SettingsTab::create() { return new SettingsTab(); }
