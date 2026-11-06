//
//  host_card.cpp
//  Equinox
//

#include "host_card.hpp"
#include "add_host_tab.hpp"
#include "app_list_view.hpp"
#include "capacity_test_view.hpp"
#include "helper.hpp"
#include "main_tabs_view.hpp"
#include "streaming_view.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

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

// MARK: - MonitorView

namespace {
NVGcolor statusColor(HostCardState state) {
    switch (state) {
    case HostCardState::ONLINE:
    case HostCardState::RUNNING:
        return themeColor("equinox/online");
    case HostCardState::ASLEEP:
        return themeColor("equinox/status_moon");
    default:
        return themeColor("equinox/muted");
    }
}
} // namespace

void MonitorView::draw(NVGcontext* vg, float x, float y, float width,
                       float height, Style style, FrameContext* ctx) {
    const float cx = x + width / 2;
    const NVGcolor sun = themeColor("equinox/sun");

    // Stage behind the monitor.
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, width, height, 12);
    nvgFillColor(vg, a(themeColor("equinox/card_inner")));
    nvgFill(vg);

    if (add) {
        const float cy = y + height / 2, r = 28, arm = 11;
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, r);
        if (primary) {
            nvgFillColor(vg, a(sun));
            nvgFill(vg);
        } else {
            nvgStrokeColor(vg, a(themeColor("equinox/horizon_off")));
            nvgStrokeWidth(vg, 2);
            nvgStroke(vg);
        }
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - arm, cy);
        nvgLineTo(vg, cx + arm, cy);
        nvgMoveTo(vg, cx, cy - arm);
        nvgLineTo(vg, cx, cy + arm);
        nvgLineCap(vg, NVG_ROUND);
        nvgStrokeColor(vg, a(primary ? themeColor("equinox/pill_text")
                                     : themeColor("brls/text")));
        nvgStrokeWidth(vg, 3);
        nvgStroke(vg);
        nvgLineCap(vg, NVG_BUTT);
        return;
    }

    const bool on = isOnline(state);
    const float sw = 104, sh = 64, neck = 12, baseH = 5;
    const float sx = cx - sw / 2;
    const float sy = y + (height - (sh + neck + baseH)) / 2;

    // Stand.
    nvgBeginPath(vg);
    nvgRect(vg, cx - 6, sy + sh, 12, neck);
    nvgRoundedRect(vg, cx - 24, sy + sh + neck, 48, baseH, baseH / 2);
    nvgFillColor(vg, a(themeColor("equinox/horizon_off")));
    nvgFill(vg);

    // Screen.
    nvgBeginPath(vg);
    nvgRoundedRect(vg, sx, sy, sw, sh, 8);
    nvgFillColor(vg, a(themeColor(on ? "equinox/sky_day" : "equinox/sky_night")));
    nvgFill(vg);

    nvgSave(vg);
    nvgIntersectScissor(vg, sx, sy, sw, sh);
    const float scy = sy + sh / 2;
    switch (state) {
    case HostCardState::ONLINE:
    case HostCardState::RUNNING: {
        const float r = 13;
        NVGcolor glowIn = sun, glowOut = sun;
        glowIn.a = 0.35f;
        glowOut.a = 0;
        nvgBeginPath(vg);
        nvgCircle(vg, cx, scy, r * 2.6f);
        nvgFillPaint(vg, nvgRadialGradient(vg, cx, scy, r, r * 2.6f, a(glowIn),
                                           a(glowOut)));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, cx, scy, r);
        nvgFillColor(vg, a(sun));
        nvgFill(vg);
        break;
    }
    case HostCardState::ASLEEP: {
        nvgBeginPath(vg);
        nvgCircle(vg, cx, scy, 13);
        nvgFillColor(vg, a(themeColor("equinox/moon")));
        nvgFill(vg);
        // Bite out of the moon in the screen color makes the crescent.
        nvgBeginPath(vg);
        nvgCircle(vg, cx + 7, scy - 5, 11);
        nvgFillColor(vg, a(themeColor("equinox/sky_night")));
        nvgFill(vg);
        break;
    }
    case HostCardState::FETCHING: {
        NVGcolor dim = sun;
        dim.a = 0.3f;
        nvgBeginPath(vg);
        nvgCircle(vg, cx, scy, 12);
        nvgFillColor(vg, a(dim));
        nvgFill(vg);
        break;
    }
    case HostCardState::UNREACHABLE:
        break;
    }
    nvgRestore(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, sx, sy, sw, sh, 8);
    nvgStrokeColor(vg, a(themeColor(on ? "equinox/horizon_on" : "equinox/horizon_off")));
    nvgStrokeWidth(vg, 2.5f);
    nvgStroke(vg);

    // Status light on the screen corner, ringed in the stage color.
    nvgBeginPath(vg);
    nvgCircle(vg, sx + sw - 2, sy + 2, 8);
    nvgFillColor(vg, a(themeColor("equinox/card_inner")));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, sx + sw - 2, sy + 2, 5);
    nvgFillColor(vg, a(statusColor(state)));
    nvgFill(vg);
}

// MARK: - HostCard

HostCard::HostCard(const Host& host) : host(host) {
    this->inflateFromXMLRes("xml/cells/host_card.xml");

    name->setText(host.hostname);
    info->setText(" ");

    this->addGestureRecognizer(new TapGestureRecognizer(this));
    this->registerClickAction([this](View* view) {
        press([this] { activate(); });
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
            supports4K = result.value().supports4K;
            if (currentGame == 0) {
                applyState(HostCardState::ONLINE);
            } else {
                runningApp = AppInfo{"", currentGame};
                applyState(HostCardState::RUNNING);
            }

            // The app list gives the game count, and the running game's name.
            ASYNC_RETAIN
            GameStreamClient::instance().applist(
                this->host, [ASYNC_TOKEN, generation, currentGame](
                                const GSResult<AppInfoList>& apps) {
                    ASYNC_RELEASE

                    if (generation != this->refreshGeneration)
                        return;

                    if (apps.isSuccess()) {
                        gameCount = (int)apps.value().size();
                        favoriteCount = 0;
                        for (const AppInfo& app : apps.value())
                            if (Settings::instance().is_favorite(this->host,
                                                                 app.app_id))
                                favoriteCount++;
                        updateInfo();
                    }

                    if (currentGame == 0 || state != HostCardState::RUNNING)
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
                    detail->setText(appName);
                });
        });
}

void HostCard::updateInfo() {
    if (gameCount < 0) {
        info->setText(" ");
        return;
    }
    std::string text = gameCount == 1
                           ? "hosts/games_one"_i18n
                           : brls::getStr("hosts/games_count", gameCount);
    if (favoriteCount > 0)
        text += "  \u00b7  " +
                (favoriteCount == 1
                     ? "hosts/favorites_one"_i18n
                     : brls::getStr("hosts/favorites_count", favoriteCount));
    if (supports4K)
        text += "  \u00b7  4K";
    info->setText(text);
}

void HostCard::applyState(HostCardState newState) {
    state = newState;
    monitor->setState(newState);

    const NVGcolor color = statusColor(newState);
    statusDot->setBackgroundColor(color);
    status->setTextColor(color);
    detail->setText(GameStreamClient::instance().active_address(host));

    std::string action = "hints/ok"_i18n;
    switch (newState) {
    case HostCardState::FETCHING:
        status->setText("hosts/status_fetching"_i18n);
        break;
    case HostCardState::ONLINE:
        status->setText("hosts/status_online"_i18n);
        action = "hosts/action_open"_i18n;
        break;
    case HostCardState::RUNNING:
        status->setText("hosts/running_caption"_i18n);
        detail->setText(runningApp.name.empty() ? "hosts/running_loading"_i18n
                                                : runningApp.name);
        action = "hosts/action_resume"_i18n;
        break;
    case HostCardState::ASLEEP:
        status->setText("hosts/status_asleep"_i18n);
        action = "hosts/action_wake"_i18n;
        break;
    case HostCardState::UNREACHABLE:
        status->setText("hosts/status_unreachable"_i18n);
        action = "hosts/action_retry"_i18n;
        break;
    }
    updateActionHint(ControllerButton::BUTTON_A, action);

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

void HostCard::openLibrary() {
    MainTabs::presentWithHeader(this, new AppListView(this->host));
}

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

// MARK: - AddHostCard

AddHostCard::AddHostCard(bool primary) {
    this->inflateFromXMLRes("xml/cells/host_card.xml");

    // An open slot: no fill, dashed outline drawn in draw().
    this->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
    monitor->setAddMode(primary);
    name->setText("hosts/add_title"_i18n);
    statusDot->setVisibility(Visibility::GONE);
    status->setSingleLine(false);
    status->setFontSize(14);
    status->setTextColor(themeColor("equinox/muted"));
    status->setText("hosts/add_body"_i18n);
    detail->setVisibility(Visibility::GONE);
    info->setVisibility(Visibility::GONE);

    this->addGestureRecognizer(new TapGestureRecognizer(this));
    this->registerClickAction([this](View* view) {
        press([this] {
            auto* screen = new AddHostTab();
            screen->getAppletFrameItem()->title = "hosts/add_screen"_i18n;
            MainTabs::presentWithHeader(this, screen);
        });
        return true;
    });
    updateActionHint(ControllerButton::BUTTON_A, "hosts/add_action"_i18n);
}

void AddHostCard::draw(NVGcontext* vg, float x, float y, float width,
                       float height, Style style, FrameContext* ctx) {
    PressableBox::draw(vg, x, y, width, height, style, ctx);

    // Walk the rounded outline and stroke every other stretch of it.
    const float r = 16, inset = 1;
    const float x0 = x + inset, y0 = y + inset;
    const float w = width - 2 * inset, h = height - 2 * inset;
    const float horizontal = w - 2 * r, vertical = h - 2 * r;
    const float arc = NVG_PI * r / 2;
    const float perimeter = 2 * horizontal + 2 * vertical + 4 * arc;
    // top, top-right corner, right, bottom-right corner, bottom, ...
    const float lengths[] = {horizontal, arc, vertical, arc,
                             horizontal, arc, vertical, arc};

    auto point = [&](float t) -> std::pair<float, float> {
        t = std::fmod(t, perimeter);
        int i = 0;
        while (i < 7 && t > lengths[i]) {
            t -= lengths[i];
            i++;
        }
        const float f = lengths[i] > 0 ? t / lengths[i] : 0;
        auto corner = [&](float ccx, float ccy, float start) {
            const float angle = start + f * NVG_PI / 2;
            return std::pair<float, float>(ccx + std::cos(angle) * r,
                                           ccy + std::sin(angle) * r);
        };
        switch (i) {
        case 0: return {x0 + r + t, y0};
        case 1: return corner(x0 + w - r, y0 + r, -NVG_PI / 2);
        case 2: return {x0 + w, y0 + r + t};
        case 3: return corner(x0 + w - r, y0 + h - r, 0);
        case 4: return {x0 + w - r - t, y0 + h};
        case 5: return corner(x0 + r, y0 + h - r, NVG_PI / 2);
        case 6: return {x0, y0 + h - r - t};
        default: return corner(x0 + r, y0 + r, NVG_PI);
        }
    };

    const float dash = 9, gap = 7, step = 3;
    const int dashes = std::max(1, (int)std::round(perimeter / (dash + gap)));
    const float period = perimeter / dashes;
    const float onLength = period * dash / (dash + gap);

    nvgBeginPath(vg);
    for (int d = 0; d < dashes; d++) {
        const float start = d * period;
        auto [px, py] = point(start);
        nvgMoveTo(vg, px, py);
        for (float t = step; t < onLength + step; t += step) {
            auto [qx, qy] = point(start + std::min(t, onLength));
            nvgLineTo(vg, qx, qy);
        }
    }
    nvgStrokeColor(vg, a(themeColor("equinox/horizon_off")));
    nvgStrokeWidth(vg, 1.6f);
    nvgStroke(vg);
}
