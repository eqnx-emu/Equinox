#include "about_tab.hpp"
#include <fmt/core.h>

#include <utility>

#ifdef __SWITCH__
#include <switch.h>
#endif

using namespace brls;

void openWebpage(std::string url) {
    Application::getPlatform()->openBrowser(std::move(url));
}

AboutTab::AboutTab() {
    this->inflateFromXMLRes("xml/tabs/about.xml");

    ThemeVariant variant = Application::getThemeVariant();
    std::string themePart =
        variant == brls::ThemeVariant::DARK ? "_dark" : "_light";
    logo->setImageFromRes("img/equinox_logo" + themePart + ".png");

    std::string subtitle = fmt::format(fmt::runtime("about/version"_i18n), APP_VERSION);
    versionLabel->setSubtitle(subtitle);

    std::string githubLink = "https://github.com/luizfbalves/Equinox";
    github->addGestureRecognizer(new TapGestureRecognizer(github));
    github->title->setText("about/link_github"_i18n);
    github->subtitle->setText(githubLink);
    github->image->setImageFromRes("img/links/github" + themePart + ".png");
    github->registerClickAction([githubLink](View* view) {
        openWebpage(githubLink);
        return true;
    });

    std::string originalLink = "https://github.com/XITRIX/Moonlight-Switch";
    original->addGestureRecognizer(new TapGestureRecognizer(original));
    original->title->setText("about/link_original"_i18n);
    original->subtitle->setText(originalLink);
    original->image->setImageFromRes("img/links/github" + themePart + ".png");
    original->registerClickAction([originalLink](View* view) {
        openWebpage(originalLink);
        return true;
    });
}

brls::View* AboutTab::create() { return new AboutTab(); }
