//
//  app_theme.cpp
//  Equinox
//

#include "app_theme.hpp"
#include "home_view.hpp"

#include <borealis.hpp>

using namespace brls;

namespace app_theme {

namespace {
ThemeVariant systemVariant = ThemeVariant::DARK;
}

void rememberSystem() { systemVariant = Application::getThemeVariant(); }

void apply(int setting) {
    const ThemeVariant variant = setting == 1   ? ThemeVariant::LIGHT
                                 : setting == 2 ? ThemeVariant::DARK
                                                : systemVariant;
    if (Application::getThemeVariant() != variant)
        Application::getPlatform()->setThemeVariant(variant);
}

void reload(const std::string& settingsGroup) {
    // After the selector's dropdown has closed: the views being replaced
    // may still be on the call stack now.
    brls::delay(250, [settingsGroup] {
        auto stack = Application::getActivitiesStack();
        if (stack.empty())
            return;
        // Anything opened over the main screen goes first.
        for (size_t i = 1; i < stack.size(); i++)
            Application::popActivity(TransitionAnimation::NONE);
        Activity* main = stack.front();
        View* content = View::createFromXMLResource("activity/main.xml");
        // Focus moves to the new screen before the old one is deleted:
        // giveFocus calls onFocusLost on the view that has it.
        Application::giveFocus(content);
        main->setContentView(content);
        content->willAppear(true);
        HomeView::openSettings(settingsGroup);
    });
}

} // namespace app_theme
