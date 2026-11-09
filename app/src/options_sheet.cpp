//
//  options_sheet.cpp
//  Equinox
//

#include "options_sheet.hpp"

#include "bold_label.hpp"

#include <borealis/views/bottom_bar.hpp>

using namespace brls;
using namespace brls::literals;

namespace {
constexpr float kPanelWidth = 440;

NVGcolor themeColor(const char* name) { return Application::getTheme()[name]; }

void closeThen(std::function<void()> action) {
    if (!action)
        action = [] {};
    Application::popActivity(TransitionAnimation::FADE, std::move(action));
}
} // namespace

void OptionsSheet::open(const std::string& title, std::vector<Item> items) {
    Application::pushActivity(new Activity(new OptionsSheet(title, std::move(items))),
                              TransitionAnimation::FADE);
}

OptionsSheet::OptionsSheet(const std::string& title, std::vector<Item> items)
    : Box(Axis::COLUMN) {
    setWidth(Application::contentWidth);
    setHeight(Application::contentHeight);

    auto* area = new Box(Axis::ROW);
    area->setGrow(1);
    area->setBackgroundColor(themeColor("brls/backdrop"));
    addView(area);

    panel = new Box(Axis::COLUMN);
    panel->setWidth(kPanelWidth);
    panel->setPadding(40, 24, 24, 24);
    panel->setBackgroundColor(themeColor("brls/background"));
    area->addView(panel);

    auto* heading = new BoldLabel();
    heading->setFontSize(26);
    heading->setMarginLeft(16);
    heading->setMarginBottom(20);
    heading->setText(title);
    panel->addView(heading);

    for (Item& item : items) {
        auto* row = new Box(Axis::ROW);
        row->setHeight(60);
        row->setPadding(0, 16, 0, 16);
        row->setAlignItems(AlignItems::CENTER);
        row->setCornerRadius(10);
        row->setHighlightCornerRadius(12);
        row->setFocusable(true);
        row->addGestureRecognizer(new TapGestureRecognizer(row));
        row->registerClickAction([action = std::move(item.action)](View*) {
            closeThen(action);
            return true;
        });

        auto* label = new Label();
        label->setFontSize(20);
        label->setSingleLine(true);
        label->setGrow(1);
        label->setText(item.label);
        row->addView(label);
        panel->addView(row);
    }

    auto* outside = new Box();
    outside->setGrow(1);
    outside->addGestureRecognizer(new TapGestureRecognizer([](TapGestureStatus status, Sound*) {
        if (status.state == GestureState::END)
            closeThen(nullptr);
    }));
    area->addView(outside);

    auto* bar = new BottomBar();
    bar->setBackgroundColor(themeColor("brls/background"));
    addView(bar);

    registerAction("brls/hints/back"_i18n, BUTTON_B, [](View*) {
        closeThen(nullptr);
        return true;
    });
}

void OptionsSheet::show(std::function<void(void)> cb, bool animate,
                        float animationDuration) {
    if (animate) {
        panel->setTranslationX(-kPanelWidth);
        slide.stop();
        slide.reset(-kPanelWidth);
        slide.addStep(0.0f, 220, EasingFunction::cubicOut);
        slide.setTickCallback([this] { panel->setTranslationX(slide); });
        slide.start();
    }
    Box::show(std::move(cb), animate, animationDuration);
}
