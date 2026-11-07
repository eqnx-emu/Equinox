//
//  add_host_tab.cpp
//  Moonlight
//
//  Created by XITRIX on 26.05.2021.
//

#include "add_host_tab.hpp"
#include "helper.hpp"
#include "home_view.hpp"
#include <memory>
#include <chrono>
#include <random>

using namespace brls;
using namespace brls::literals;

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif


namespace {
std::string strip_ipv4_port(const std::string& address) {
    const auto firstColon = address.find(':');
    if (firstColon == std::string::npos) {
        return address;
    }

    if (address.find(':', firstColon + 1) != std::string::npos) {
        return address;
    }

    return address.substr(0, firstColon);
}

bool is_private_ipv4(const in_addr& address) {
    const uint32_t value = ntohl(address.s_addr);
    const uint8_t a = (value >> 24) & 0xFF;
    const uint8_t b = (value >> 16) & 0xFF;

    return a == 10 || a == 127 || (a == 169 && b == 254) ||
           (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31);
}

bool should_store_manual_address_as_remote(const std::string& address) {
    const auto hostPart = strip_ipv4_port(address);
    if (hostPart.empty()) {
        return false;
    }

    in_addr parsed{};
    if (inet_pton(AF_INET, hostPart.c_str(), &parsed) != 1) {
        return false;
    }

    return !is_private_ipv4(parsed);
}
}

AddHostTab::AddHostTab() {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/add_host.xml");

    hostIP->init("add_host/host_ip"_i18n, "");
    hostIP->setPlaceholder("192.168.1.109:47989");
    hostIP->setHint("192.168.1.109:47989");

    connect->setText("add_host/connect"_i18n);
    connect->registerClickAction([this](View* view) {
        Host host;
        const auto inputAddress = hostIP->getValue();
        if (should_store_manual_address_as_remote(inputAddress)) {
            host.remoteAddress = inputAddress;
        } else {
            host.address = inputAddress;
        }
        connectHost(host);
        return true;
    });

    if (GameStreamClient::can_find_host())
        findHost();
    else {
        searchHeader->setTitle("add_host/search_error"_i18n);
        loader->setVisibility(brls::Visibility::GONE);
    }

    registerAction("add_host/search_refresh"_i18n, ControllerButton::BUTTON_X,
                   [this](View* view) {
                       findHost();
                       return true;
                   });
    setActionAvailable(BUTTON_X, GameStreamClient::can_find_host());
}

void AddHostTab::appendSearchHosts(const std::vector<Host>& hosts) {
    for (const Host& host : hosts) {
        const auto displayAddress = host.preferred_address();
        if (displayAddress.empty() || searchBoxIpExists(displayAddress))
            continue;

        auto hostButton = new brls::DetailCell();
        hostButton->setText(host.hostname);
        hostButton->setDetailText(displayAddress);
        hostButton->setDetailTextColor(
            brls::Application::getTheme()["brls/text_disabled"]);
        hostButton->registerClickAction([this, host](View* view) {
            connectHost(host);
            return true;
        });
        searchBox->addView(hostButton);
    }
}

bool AddHostTab::searchBoxIpExists(const std::string& ip) {
    return std::any_of(searchBox->getChildren().begin(), searchBox->getChildren().end(), [ip](View* child) {
        auto cell = dynamic_cast<DetailCell*>(child);
        return cell->detail->getFullText() == ip;
    });
}

void AddHostTab::findHost() {
    stopSearchHost();
    const uint64_t generation = searchGeneration;
    searchBox->clearViews();
    searchHeader->setTitle("add_host/search"_i18n);
    loader->setVisibility(brls::Visibility::VISIBLE);
    // Called once per PC found. No token: the destructor cancels the search,
    // and find_hosts drops every answer from a cancelled search.
    GameStreamClient::find_hosts(
        [this, generation](const GSResult<std::vector<Host>>& result) {
            if (generation != searchGeneration) {
                return;
            }

            if (result.isSuccess()) {
                appendSearchHosts(result.value());
            } else {
                loader->setVisibility(brls::Visibility::GONE);
                showError(result.error(), [] {});
            }
        });
}

void AddHostTab::stopSearchHost() {
    searchGeneration++;
    GameStreamClient::cancel_find_hosts();
}

void AddHostTab::connectHost(const Host& host) {
    pauseSearching();

    Dialog* loaderView = createLoadingDialog("add_host/try_connect"_i18n);
    loaderView->open();

    ASYNC_RETAIN
    GameStreamClient::instance().connect(
        host, [ASYNC_TOKEN, loaderView, host](const GSResult<SERVER_DATA>& result) {
            ASYNC_RELEASE
            loaderView->close([this, result, host] {
                if (result.isSuccess()) {
                    Host pairedHost = host;
                    pairedHost.hostname = result.value().hostname;
                    pairedHost.mac = result.value().mac;

                    if (result.value().paired) {
                        showAlert("add_host/paired_error"_i18n, [pairedHost] {
                            Settings::instance().add_host(pairedHost);
                            HomeView::hostAdded();
                        });

                        return;
                    }

                    // random_device is a fixed-seed generator on Switch:
                    // the clock makes each PIN different.
                    std::random_device device;
                    std::seed_seq seed{
                        device(),
                        (unsigned)std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count(),
                        (unsigned)std::chrono::system_clock::now()
                            .time_since_epoch()
                            .count()};
                    std::mt19937 random(seed);
                    const auto pin = fmt::format(
                        "{:04d}",
                        std::uniform_int_distribution<int>(0, 9999)(random));

                    // Cancel closes the dialog right away; the pending
                    // request is ignored when it ends.
                    auto cancelled = std::make_shared<bool>(false);
                    brls::Dialog* dialog = createLoadingDialog(
                        "add_host/pair_prefix"_i18n + pin +
                            "add_host/pair_postfix"_i18n,
                        [cancelled] {
                            *cancelled = true;
                            AddHostTab::startSearching();
                        });
                    dialog->open();

                    ASYNC_RETAIN
                    GameStreamClient::instance().pair(
                        pairedHost, pin,
                        [ASYNC_TOKEN, pairedHost, dialog,
                         cancelled](const GSResult<bool>& result) {
                            ASYNC_RELEASE
                            if (*cancelled || isDialogClosing(dialog))
                                return;
                            dialog->dismiss([result, pairedHost] {
                                if (result.isSuccess()) {
                                    Settings::instance().add_host(pairedHost);
                                    HomeView::hostAdded();
                                    AddHostTab::startSearching();
                                } else {
                                    showError(result.error(), [] {
                                        AddHostTab::startSearching();
                                    });
                                }
                            });
                        });
                } else {
                    showError(result.error(),
                              [] { AddHostTab::startSearching(); });
                }
            });
        });
}

void AddHostTab::pauseSearching() {
}

void AddHostTab::startSearching() {
}

AddHostTab::~AddHostTab() {
    stopSearchHost();
}

brls::View* AddHostTab::create() {
    // Called by the XML engine to create a new AddHostTab
    return new AddHostTab();
}
