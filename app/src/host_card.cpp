//
//  host_card.cpp
//  Equinox
//

#include "host_card.hpp"
#include "app_list_view.hpp"
#include "capacity_test_view.hpp"
#include "helper.hpp"
#include "main_tabs_view.hpp"
#include "streaming_view.hpp"

using namespace brls;
using namespace brls::literals;

namespace {
NVGcolor themeColor(const std::string& name) {
    return Application::getTheme().getColor(name);
}

bool isOnline(HostCardState state) {
    return state == HostCardState::ONLINE || state == HostCardState::RUNNING;
}
} // namespace

// MARK: - HorizonView

void HorizonView::draw(NVGcontext* vg, float x, float y, float width,
                       float height, Style style, FrameContext* ctx) {
    const float radius = 16;
    const float horizonY = y + height * 0.74f;
    const float skyHeight = horizonY - y;
    const float cx = x + width / 2;
    const bool day = isOnline(state);

    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, x, y, width, skyHeight, radius, radius, 0, 0);
    nvgFillColor(vg, a(themeColor(day ? "equinox/sky_day" : "equinox/sky_night")));
    nvgFill(vg);

    // Everything below is clipped to the sky, so a body sitting on the
    // horizon is cut by it like a rising sun.
    nvgSave(vg);
    nvgIntersectScissor(vg, x, y, width, skyHeight);

    const NVGcolor sun = themeColor("equinox/sun");
    const NVGcolor moon = themeColor("equinox/moon");

    switch (state) {
    case HostCardState::ONLINE:
    case HostCardState::RUNNING: {
        const float cy = horizonY - 16;
        const float r = 24;
        NVGcolor glowInner = sun;
        glowInner.a = 0.35f;
        NVGcolor glowOuter = sun;
        glowOuter.a = 0;
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, r * 2.6f);
        nvgFillPaint(vg, nvgRadialGradient(vg, cx, cy, r, r * 2.6f,
                                           a(glowInner), a(glowOuter)));
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, r);
        nvgFillColor(vg, a(sun));
        nvgFill(vg);
        break;
    }
    case HostCardState::ASLEEP: {
        const float cy = y + skyHeight * 0.48f;
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, 18);
        nvgFillColor(vg, a(moon));
        nvgFill(vg);

        // Bite out of the moon in the sky color makes the crescent.
        nvgBeginPath(vg);
        nvgCircle(vg, cx + 9, cy - 6, 16);
        nvgFillColor(vg, a(themeColor("equinox/sky_night")));
        nvgFill(vg);

        nvgFillColor(vg, a(moon));
        nvgBeginPath(vg);
        nvgCircle(vg, x + width * 0.22f, y + skyHeight * 0.3f, 1.6f);
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, x + width * 0.78f, y + skyHeight * 0.62f, 1.6f);
        nvgFill(vg);
        break;
    }
    case HostCardState::FETCHING: {
        NVGcolor dim = sun;
        dim.a = 0.35f;
        nvgBeginPath(vg);
        nvgCircle(vg, cx, horizonY + 6, 22);
        nvgFillColor(vg, a(dim));
        nvgFill(vg);
        break;
    }
    case HostCardState::UNREACHABLE:
        nvgBeginPath(vg);
        nvgCircle(vg, cx, horizonY + 2, 20);
        nvgStrokeColor(vg, a(themeColor("equinox/horizon_off")));
        nvgStrokeWidth(vg, 2);
        nvgStroke(vg);
        break;
    }

    nvgRestore(vg);

    nvgBeginPath(vg);
    nvgMoveTo(vg, x, horizonY);
    nvgLineTo(vg, x + width, horizonY);
    nvgStrokeColor(vg, a(themeColor(day ? "equinox/horizon_on" : "equinox/horizon_off")));
    nvgStrokeWidth(vg, 1.5f);
    nvgStroke(vg);
}

// MARK: - HostCard

HostCard::HostCard(const Host& host) : host(host) {
    this->inflateFromXMLRes("xml/cells/host_card.xml");

    name->setText(host.hostname);

    this->addGestureRecognizer(new TapGestureRecognizer(this));
    this->registerClickAction([this](View* view) {
        activate();
        return true;
    });

    registerAction("capacity/action"_i18n, ControllerButton::BUTTON_X,
                   [this](View* view) {
                       if (isOnline(state))
                           CapacityTestView::open(this->host);
                       return true;
                   });

    // Y is contextual: wake a sleeping PC, or open the library while a game
    // runs (A resumes the game then).
    registerAction("hosts/wake"_i18n, ControllerButton::BUTTON_Y,
                   [this](View* view) {
                       if (state == HostCardState::ASLEEP)
                           wake();
                       else if (state == HostCardState::RUNNING)
                           openLibrary();
                       return true;
                   });

    registerAction("hosts/options"_i18n, ControllerButton::BUTTON_START,
                   [this](View* view) {
                       openOptions();
                       return true;
                   });

    refresh();
}

void HostCard::refresh() {
    // willAppear can fire twice in a row (tab added, then activity shown);
    // one query at a time is enough.
    if (requestInFlight)
        return;
    requestInFlight = true;

    const auto generation = ++refreshGeneration;
    applyState(HostCardState::FETCHING);

    ASYNC_RETAIN
    GameStreamClient::instance().connect(
        host, [ASYNC_TOKEN, generation](const GSResult<SERVER_DATA>& result) {
            ASYNC_RELEASE

            if (generation != this->refreshGeneration)
                return;
            requestInFlight = false;

            if (!result.isSuccess()) {
                applyState(GameStreamClient::can_wake_up_host(this->host)
                               ? HostCardState::ASLEEP
                               : HostCardState::UNREACHABLE);
                return;
            }

            const int currentGame = result.value().currentGame;
            if (currentGame == 0) {
                applyState(HostCardState::ONLINE);
                return;
            }

            runningApp = AppInfo{"", currentGame};
            applyState(HostCardState::RUNNING);

            ASYNC_RETAIN
            GameStreamClient::instance().applist(
                this->host, [ASYNC_TOKEN, generation, currentGame](
                                const GSResult<AppInfoList>& apps) {
                    ASYNC_RELEASE

                    if (generation != this->refreshGeneration ||
                        state != HostCardState::RUNNING)
                        return;

                    std::string appName = "hosts/running_unknown"_i18n;
                    if (apps.isSuccess()) {
                        for (const AppInfo& app : apps.value()) {
                            if (app.app_id == currentGame) {
                                appName = app.name;
                                break;
                            }
                        }
                    }
                    runningApp.name = appName;
                    infoValue->setText(appName);
                });
        });
}

void HostCard::applyState(HostCardState newState) {
    state = newState;
    horizon->setState(newState);

    const NVGcolor sun = Application::getTheme().getColor("equinox/status_on");
    const NVGcolor moon = Application::getTheme().getColor("equinox/status_moon");
    const NVGcolor muted = Application::getTheme().getColor("equinox/muted");
    const NVGcolor text = Application::getTheme().getColor("brls/text");
    const NVGcolor pillText = Application::getTheme().getColor("equinox/pill_text");

    // Primary pill: filled with the sun color. Secondary: outlined.
    auto primaryPill = [&](const std::string& label) {
        actionPill->setBackgroundColor(themeColor("equinox/sun"));
        actionPill->setBorderThickness(0);
        actionLabel->setTextColor(pillText);
        actionLabel->setText(label);
    };
    auto outlinePill = [&](const std::string& label, NVGcolor border) {
        actionPill->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
        actionPill->setBorderColor(border);
        actionPill->setBorderThickness(1.5f);
        actionLabel->setTextColor(text);
        actionLabel->setText(label);
    };

    infoBox->setVisibility(Visibility::VISIBLE);
    infoCaption->setText("hosts/address"_i18n);
    infoValue->setText(GameStreamClient::instance().active_address(host));

    switch (newState) {
    case HostCardState::FETCHING:
        status->setText("hosts/status_fetching"_i18n);
        status->setTextColor(muted);
        outlinePill("host/wait"_i18n, muted);
        break;
    case HostCardState::ONLINE:
        status->setText("hosts/status_online"_i18n);
        status->setTextColor(sun);
        primaryPill("hosts/action_open"_i18n);
        break;
    case HostCardState::RUNNING:
        status->setText("hosts/status_online"_i18n);
        status->setTextColor(sun);
        infoCaption->setText("hosts/running_caption"_i18n);
        infoValue->setText(runningApp.name.empty() ? "hosts/running_loading"_i18n
                                                   : runningApp.name);
        primaryPill("hosts/action_resume"_i18n);
        break;
    case HostCardState::ASLEEP:
        status->setText("hosts/status_asleep"_i18n);
        status->setTextColor(moon);
        outlinePill("hosts/action_wake"_i18n, moon);
        break;
    case HostCardState::UNREACHABLE:
        status->setText("hosts/status_unreachable"_i18n);
        status->setTextColor(muted);
        outlinePill("hosts/action_retry"_i18n, muted);
        break;
    }

    setActionAvailable(ControllerButton::BUTTON_X, isOnline(newState));
    setActionAvailable(ControllerButton::BUTTON_Y,
                       newState == HostCardState::ASLEEP ||
                           newState == HostCardState::RUNNING);
    updateActionHint(ControllerButton::BUTTON_Y,
                     newState == HostCardState::RUNNING ? "hosts/library"_i18n
                                                        : "hosts/wake"_i18n);
}

void HostCard::activate() {
    switch (state) {
    case HostCardState::ONLINE:
        openLibrary();
        break;
    case HostCardState::RUNNING:
        resumeRunningApp();
        break;
    case HostCardState::ASLEEP:
        wake();
        break;
    case HostCardState::UNREACHABLE:
        refresh();
        break;
    case HostCardState::FETCHING:
        break;
    }
}

void HostCard::openLibrary() { this->present(new AppListView(this->host)); }

void HostCard::resumeRunningApp() {
    if (runningApp.app_id == 0)
        return;

    // Same launch path as AppCell: resuming the running app reconnects to it.
    auto* frame = new AppletFrame(new StreamingView(host, runningApp));
    frame->setBackground(ViewBackground::NONE);
    frame->setHeaderVisibility(brls::Visibility::GONE);
    frame->setFooterVisibility(brls::Visibility::GONE);
    Application::pushActivity(new Activity(frame));
}

void HostCard::wake() {
    if (!GameStreamClient::can_wake_up_host(host))
        return;

    const auto wakeRequestId = ++wakeRequestGeneration;
    canceledWakeRequestGeneration = 0;

    Dialog* loader = createLoadingDialog(
        "host/wake_up_message"_i18n, [this, wakeRequestId] {
            if (wakeRequestGeneration == wakeRequestId)
                canceledWakeRequestGeneration = wakeRequestId;
        });
    loader->open();

    ASYNC_RETAIN
    GameStreamClient::wake_up_host(
        host, [ASYNC_TOKEN, loader, wakeRequestId](const GSResult<bool>& result) {
            ASYNC_RELEASE

            if (wakeRequestId != wakeRequestGeneration ||
                canceledWakeRequestGeneration == wakeRequestId)
                return;

            loader->close([this, result, wakeRequestId] {
                if (wakeRequestId != wakeRequestGeneration)
                    return;

                if (result.isSuccess())
                    refresh();
                else
                    showError("host/wake_up_error"_i18n);
            });
        });
}

void HostCard::openOptions() {
    const Host hostCopy = host;
    auto* dialog = new Dialog(host.hostname);

    dialog->addButton("host/rename"_i18n, [hostCopy] {
        Application::getPlatform()->getImeManager()->openForText(
            [hostCopy](const std::string& text) {
                Host renamed = hostCopy;
                renamed.hostname = text;
                Settings::instance().add_host(renamed);
                MainTabs::getInstanse()->refillTabs();
            },
            "host/rename_title"_i18n, "", 60, hostCopy.hostname, 0);
    });

    dialog->addButton("common/remove"_i18n, [hostCopy] {
        auto* confirm = new Dialog("host/remove_message"_i18n);
        confirm->addButton("common/cancel"_i18n, [] {});
        confirm->addButton("common/remove"_i18n, [hostCopy] {
            Settings::instance().remove_host(hostCopy);
            MainTabs::getInstanse()->refillTabs();
        });
        confirm->open();
    });

    dialog->addButton("common/cancel"_i18n, [] {});
    dialog->open();
}
