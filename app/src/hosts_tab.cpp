//
//  hosts_tab.cpp
//  Equinox
//

#include "hosts_tab.hpp"
#include <Settings.hpp>

using namespace brls;
using namespace brls::literals;

HostsTab::HostsTab() {
    this->inflateFromXMLRes("xml/tabs/hosts.xml");

    title->setText("hosts/title"_i18n);

    const auto hosts = Settings::instance().hosts();
    if (hosts.empty()) {
        subtitle->setText("hosts/empty"_i18n);
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
