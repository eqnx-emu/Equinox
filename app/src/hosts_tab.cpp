//
//  hosts_tab.cpp
//  Equinox
//

#include "hosts_tab.hpp"
#include <Settings.hpp>

using namespace brls;
using namespace brls::literals;

namespace {
// Shown before any PC is paired: an empty horizon and how to add one.
View* makeEmptyCard() {
    auto* card = new Box(Axis::COLUMN);
    card->setWidth(440);
    card->setCornerRadius(16);
    card->setBackgroundColor(Application::getTheme().getColor("equinox/card"));

    auto* horizon = new HorizonView();
    horizon->setState(HostCardState::UNREACHABLE);
    horizon->setHeight(104);
    card->addView(horizon);

    auto* body = new Box(Axis::COLUMN);
    body->setPadding(4, 22, 22, 22);

    auto* heading = new Label();
    heading->setFontSize(21);
    heading->setText("hosts/empty_title"_i18n);
    body->addView(heading);

    auto* text = new Label();
    text->setFontSize(15);
    text->setMarginTop(8);
    text->setTextColor(Application::getTheme().getColor("equinox/muted"));
    text->setText("hosts/empty_body"_i18n);
    body->addView(text);

    card->addView(body);
    return card;
}
} // namespace

HostsTab::HostsTab() {
    this->inflateFromXMLRes("xml/tabs/hosts.xml");

    title->setText("hosts/title"_i18n);

    const auto hosts = Settings::instance().hosts();
    if (hosts.empty()) {
        subtitle->setText("hosts/empty"_i18n);
        container->addView(makeEmptyCard());
        return;
    }
    subtitle->setText("hosts/subtitle"_i18n);

    auto* grid = new GridView(3);
    for (const Host& host : hosts) {
        auto* card = new HostCard(host);
        cards.push_back(card);
        grid->addView(card);
    }
    container->addView(grid);
}

void HostsTab::willAppear(bool resetState) {
    Box::willAppear(resetState);

    // The cards already query their host when they are created.
    if (!appeared) {
        appeared = true;
        return;
    }
    for (HostCard* card : cards)
        card->refresh();
}

View* HostsTab::create() { return new HostsTab(); }
