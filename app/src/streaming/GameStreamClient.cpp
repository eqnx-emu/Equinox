#include "GameStreamClient.hpp"
#include "Settings.hpp"
#include "WakeOnLanManager.hpp"
#include <borealis.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <set>
#include <vector>

#include <curl/curl.h>
#include <libretro-common/retro_timers.h>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <fmt/core.h>


#if defined(__SWITCH__)
#include <switch.h>

// TODO: Remove when presented in LibNX
struct ipv6_mreq {
	struct in6_addr ipv6mr_multiaddr;
	unsigned int    ipv6mr_interface;
};
#endif


extern "C" {
#include <mdns.h>
}

using namespace brls;

namespace {
void rebind_server_info(SERVER_DATA& server) {
    server.serverInfo.address = server.address.c_str();
    server.serverInfo.serverInfoAppVersion =
        server.serverInfoAppVersion.c_str();
    server.serverInfo.serverInfoGfeVersion =
        server.serverInfoGfeVersion.c_str();
}

std::string host_key(const Host& host) {
    if (!host.mac.empty()) {
        return "mac:" + host.mac;
    }
    if (!host.address.empty()) {
        return "address:" + host.address;
    }
    if (!host.remoteAddress.empty()) {
        return "remote:" + host.remoteAddress;
    }
    return "hostname:" + host.hostname;
}

void merge_discovered_host(std::vector<Host>& hosts, const Host& host) {
    auto it = std::find_if(hosts.begin(), hosts.end(), [host](const Host& existing) {
        return hosts_match(existing, host);
    });

    if (it == hosts.end()) {
        hosts.push_back(host);
        return;
    }

    if (!host.address.empty()) {
        it->address = host.address;
    }
    if (!host.remoteAddress.empty()) {
        it->remoteAddress = host.remoteAddress;
    }
    if (!host.hostname.empty()) {
        it->hostname = host.hostname;
    }
    if (!host.mac.empty()) {
        it->mac = host.mac;
    }
}

bool is_ipv4_address(const std::string& address) {
    if (address.empty()) {
        return false;
    }

    in_addr parsed{};
    return inet_pton(AF_INET, address.c_str(), &parsed) == 1;
}

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

std::string ipv4_port_suffix(const std::string& address) {
    const auto firstColon = address.find(':');
    if (firstColon == std::string::npos) {
        return "";
    }
    if (address.find(':', firstColon + 1) != std::string::npos) {
        return "";
    }
    return address.substr(firstColon);
}

bool connect_to_addresses_sync(const std::vector<std::string>& addresses,
                               std::string& connectedAddress,
                               SERVER_DATA& connectedServer,
                               std::string& error) {
    if (std::none_of(addresses.begin(), addresses.end(),
                     [](const std::string& address) {
                         return !address.empty();
                     })) {
        error = "Address is Empty";
        return false;
    }

    error = "Address is Empty";
    connectedAddress.clear();
    connectedServer = SERVER_DATA{};

    for (const auto& address : addresses) {
        if (address.empty()) {
            continue;
        }

        SERVER_DATA serverData{};
        const int status = gs_init(&serverData, address);
        if (status == GS_OK) {
            connectedAddress = address;
            connectedServer = serverData;
            return true;
        }

        error = gs_error();
    }

    return false;
}

constexpr int WAKE_POLL_ATTEMPTS = 20;
constexpr auto WAKE_POLL_INTERVAL = std::chrono::seconds(1);

}

GameStreamClient::GameStreamClient() { start(); }

void GameStreamClient::start() {}

void GameStreamClient::stop() {
    cancel_find_hosts();
}

static uint32_t get_my_ip_address() {
    uint32_t address = 0;
#if   defined(__SWITCH__)
    nifmGetCurrentIpAddress(&address);
#endif
    return address;
}

std::vector<std::string> GameStreamClient::host_addresses_for_find() {
    std::vector<std::string> addresses;

    uint32_t address = get_my_ip_address();
    bool isSucceed = address != 0;

    if (isSucceed) {
        uint32_t a = address & 0xFF;
        uint32_t b = (address >> 8) & 0xFF;
        uint32_t c = (address >> 16) & 0xFF;
        uint32_t d = (address >> 24) & 0xFF;

        for (int i = 0; i < 256; i++) {
            if (i == d) {
                continue;
            }
            addresses.push_back(std::to_string(a) + "." + std::to_string(b) +
                                "." + std::to_string(c) + "." +
                                std::to_string(i));
        }
    }
    return addresses;
}

std::string GameStreamClient::external_address_for_mdns(const std::string& address) {
    const auto localAddress = strip_ipv4_port(address);
    if (!localAddress.empty() && !is_ipv4_address(localAddress)) {
        return "";
    }

    unsigned int wanAddress = 0;
    const int err =
        LiFindExternalAddressIP4("stun.moonlight-stream.org", 3478, &wanAddress);
    if (err != 0) {
        Logger::error("Failed to get remote IPv4 address over STUN: {}", err);
        return "";
    }

    in_addr externalAddr{};
    externalAddr.s_addr = wanAddress;

    char addressBuffer[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, &externalAddr, addressBuffer, sizeof(addressBuffer)) == nullptr) {
        Logger::error("Failed to format remote IPv4 address returned by STUN");
        return "";
    }

    std::string externalAddress = addressBuffer;
    const auto portSuffix = ipv4_port_suffix(address);
    if (!portSuffix.empty()) {
        externalAddress += portSuffix;
    }

    return externalAddress;
}

bool GameStreamClient::can_find_host() {
    return get_my_ip_address() != 0;
}

static std::atomic_uint64_t findHostsGeneration = 0;

struct MdnsSearchContext {
    std::string foundHost;
};

std::string get_ip_str(const struct sockaddr *addr)
{
    char addrStr[INET6_ADDRSTRLEN];
    if (addr->sa_family == AF_INET) {
        inet_ntop(addr->sa_family, &((struct sockaddr_in*)addr)->sin_addr, addrStr, sizeof(addrStr));
        unsigned short port = htons(((struct sockaddr_in*)addr)->sin_port);
        return fmt::format("{}", addrStr);
    }
    else {
        struct sockaddr_in6* sin6 = (struct sockaddr_in6*)addr;
        inet_ntop(addr->sa_family, &sin6->sin6_addr, addrStr, sizeof(addrStr));
        unsigned short port = ntohs(((struct sockaddr_in6*)addr)->sin6_port);
        if (sin6->sin6_scope_id != 0) {
            // Link-local addresses with scope IDs are special
            return fmt::format("{}{}", addrStr, sin6->sin6_scope_id);
        }
        else {
            return fmt::format("{}", addrStr);
        }
    }
}

static int mdns_discovery_callback(int sock, const struct sockaddr* from, size_t addrlen,
                                   mdns_entry_type_t entry, uint16_t query_id, uint16_t type,
                                   uint16_t rclass, uint32_t ttl, const void* data, size_t size,
                                   size_t offset, size_t length, size_t record_offset,
                                   size_t record_length, void* user_data)
{
    auto* context = static_cast<MdnsSearchContext*>(user_data);
    if (type == MDNS_RECORDTYPE_A) {
        context->foundHost = get_ip_str(from);
    }
    return 0;
}

void GameStreamClient::find_hosts(ServerCallback<std::vector<Host>>& callback) {
    const uint64_t generation = ++findHostsGeneration;
    brls::async([callback, generation] {
        auto isCancelled = [generation]() {
            return generation != findHostsGeneration.load();
        };

        // Filled on the UI thread only, by the probes below.
        auto foundHosts = std::make_shared<std::vector<Host>>();
        // Addresses being checked or found; a failed check is dropped so a
        // later answer from a PC that was still waking up is tried again.
        auto probed = std::make_shared<std::set<std::string>>();
        auto probedMutex = std::make_shared<std::mutex>();
        MdnsSearchContext searchContext;
        size_t capacity = 2048;
        std::vector<uint8_t> buffer(capacity);
        size_t records;

        int sock = mdns_socket_open_ipv4(nullptr);
        if (sock < 0) {
            return;
        }

        auto closeSocket = [&sock]() {
            if (sock >= 0) {
                mdns_socket_close(sock);
                sock = -1;
            }
        };

        if (isCancelled()) {
            closeSocket();
            return;
        }

        if (mdns_query_send(sock, MDNS_RECORDTYPE_PTR,
                        MDNS_STRING_CONST("_nvstream._tcp.local"),
                        buffer.data(), capacity, 0)) {
            closeSocket();
            brls::sync([callback, generation] {
                if (generation != findHostsGeneration.load()) {
                    return;
                }

                callback(GSResult<std::vector<Host>>::failure(
                        "error/unknown_error"_i18n));
            });
            return;
        }

        int empty_cnt = 0;
        while (!isCancelled()) {
            searchContext.foundHost.clear();
            records = mdns_query_recv(sock, buffer.data(), capacity, mdns_discovery_callback, &searchContext, 0);
            if (isCancelled()) {
                break;
            }

            if (records == 0) {
                empty_cnt++;
            } else {
                empty_cnt = 0;

                if (searchContext.foundHost.empty()) {
                    continue;
                }
                {
                    std::lock_guard<std::mutex> lock(*probedMutex);
                    if (!probed->insert(searchContext.foundHost).second)
                        continue;
                }

                // Each answer is checked on its own thread, so a slow PC
                // doesn't stop the loop from hearing the others.
                const std::string address = searchContext.foundHost;
                brls::async([callback, generation, address, foundHosts, probed,
                             probedMutex] {
                    SERVER_DATA server_data;
                    if (gs_init(&server_data, address) != GS_OK) {
                        std::lock_guard<std::mutex> lock(*probedMutex);
                        probed->erase(address);
                        return;
                    }

                    Host host;
                    host.address = address;
                    host.remoteAddress =
                        GameStreamClient::external_address_for_mdns(address);
                    host.hostname = server_data.hostname;
                    host.mac = server_data.mac;

                    brls::sync([callback, generation, host, foundHosts] {
                        if (generation != findHostsGeneration.load()) {
                            return;
                        }
                        merge_discovered_host(*foundHosts, host);
                        callback(GSResult<std::vector<Host>>::success(*foundHosts));
                    });
                });
            }
            // Stop about 5 s after the last answer.
            if (empty_cnt >= 10) {
                break;
            }

            retro_sleep(500);
        }

        closeSocket();
    });
}

void GameStreamClient::cancel_find_hosts() {
    ++findHostsGeneration;
}

bool GameStreamClient::can_wake_up_host(const Host& host) {
    return WakeOnLanManager::can_wake_up_host(host);
}

void GameStreamClient::wake_up_host(
    const Host& host, ServerCallback<bool>& callback,
    std::function<void(int attempt, int total)> on_progress,
    std::shared_ptr<std::atomic<bool>> cancel) {
    brls::async([host, callback, on_progress, cancel] {
        auto result = WakeOnLanManager::wake_up_host(host);
        if (!result.isSuccess()) {
            brls::sync([callback, result] { callback(result); });
            return;
        }

        std::string connectedAddress;
        std::string error;
        SERVER_DATA connectedServer{};

        for (int attempt = 0; attempt < WAKE_POLL_ATTEMPTS; attempt++) {
            // Cancelled from the UI: stop polling and free the thread.
            if (cancel && cancel->load()) {
                brls::sync([callback] {
                    callback(GSResult<bool>::failure("cancelled"));
                });
                return;
            }

            if (on_progress)
                brls::sync([on_progress, attempt] {
                    on_progress(attempt + 1, WAKE_POLL_ATTEMPTS);
                });

            if (connect_to_addresses_sync(host.connection_addresses(),
                                          connectedAddress, connectedServer,
                                          error)) {
                GameStreamClient::instance().cache_server_data(
                    connectedAddress, connectedServer, host_key(host));

                brls::sync([callback] {
                    callback(GSResult<bool>::success(true));
                });
                return;
            }

            if (attempt + 1 < WAKE_POLL_ATTEMPTS) {
                std::this_thread::sleep_for(WAKE_POLL_INTERVAL);
            }
        }

        brls::sync([callback, error] {
            callback(GSResult<bool>::failure(
                error.empty() ? "Host did not come online after wake signal"
                              : error));
        });
    });
}

SERVER_DATA GameStreamClient::server_data(const std::string& address) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_server_data.find(address);
    if (it == m_server_data.end())
        return SERVER_DATA{};
    // serverInfo points into the strings, so the copy points at its own.
    SERVER_DATA copy = it->second;
    rebind_server_info(copy);
    return copy;
}

bool GameStreamClient::has_server_data(const std::string& address) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_server_data.count(address) != 0;
}

void GameStreamClient::cache_server_data(const std::string& address,
                                         const SERVER_DATA& data,
                                         const std::string& activeKey) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_server_data[address] = data;
    rebind_server_info(m_server_data[address]);
    if (!data.mac.empty()) {
        m_active_addresses["mac:" + data.mac] = address;
    }
    if (!activeKey.empty()) {
        m_active_addresses[activeKey] = address;
    }
}

void GameStreamClient::connect_to_addresses(
    const std::vector<std::string>& addresses, const std::string& activeKey,
    ServerCallback<SERVER_DATA>& callback) {
    if (std::none_of(addresses.begin(), addresses.end(),
                     [](const std::string& address) {
                         return !address.empty();
                     })) {
        callback(GSResult<SERVER_DATA>::failure("Address is Empty"));
        return;
    }

    brls::async([this, addresses, activeKey, callback] {
        std::string connectedAddress;
        std::string error;
        SERVER_DATA connectedServer{};
        connect_to_addresses_sync(addresses, connectedAddress, connectedServer,
                                  error);

        if (!connectedAddress.empty())
            cache_server_data(connectedAddress, connectedServer, activeKey);

        brls::sync([this, callback, connectedAddress, error] {
            if (connectedAddress.empty()) {
                callback(GSResult<SERVER_DATA>::failure(error));
                return;
            }

            callback(
                GSResult<SERVER_DATA>::success(server_data(connectedAddress)));
        });
    });
}

void GameStreamClient::connect(const std::string& address,
                               ServerCallback<SERVER_DATA>& callback) {
    connect_to_addresses({address}, "", callback);
}

std::string GameStreamClient::active_address(const Host& host) const {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (const auto it = m_active_addresses.find(host_key(host));
            it != m_active_addresses.end() && !it->second.empty()) {
            return it->second;
        }
    }

    return host.preferred_address();
}

void GameStreamClient::connect(const Host& host,
                               ServerCallback<SERVER_DATA>& callback) {
    connect_to_addresses(host.connection_addresses(), host_key(host), callback);
}

void GameStreamClient::pair(const std::string& address, const std::string& pin,
                            ServerCallback<bool>& callback) {
    with_cached_server_data<bool>(
        address, "Firstly call connect()...", callback,
        [this, pin](const std::string& cachedAddress, SERVER_DATA& server,
                    ServerCallback<bool>& callback) {
            int status = gs_pair(&server, (char*)pin.c_str());
            const std::string error = status == GS_OK ? "" : gs_error();
            if (status == GS_OK)
                cache_server_data(cachedAddress, server);

            brls::sync([callback, status, error] {
                if (status == GS_OK) {
                    callback(GSResult<bool>::success(true));
                } else {
                    callback(GSResult<bool>::failure(error));
                }
            });
        });
}

void GameStreamClient::pair(const Host& host, const std::string& pin,
                            ServerCallback<bool>& callback) {
    pair(active_address(host), pin, callback);
}

void GameStreamClient::applist(const std::string& address,
                               ServerCallback<AppInfoList>& callback) {
    with_cached_server_data<AppInfoList>(
        address, "Firstly call connect() & pair()...", callback,
        [](const std::string& cachedAddress, SERVER_DATA& server,
           ServerCallback<AppInfoList>& callback) {
            PAPP_LIST list;

            int status = gs_applist(&server, &list);
            if (status != GS_OK) {
                const std::string error = gs_error();
                brls::sync([callback, error] {
                    callback(GSResult<AppInfoList>::failure(error));
                });
                return;
            }

            AppInfoList app_list;

            while (list) {
                AppInfo info;
                info.name = std::string(list->name);
                info.app_id = list->id;
                app_list.push_back(info);
                list = list->next;
            }

            std::sort(app_list.begin(), app_list.end(),
                      [](const AppInfo& a, const AppInfo& b) {
                          return a.name < b.name;
                      });

            brls::sync([app_list, callback] {
                callback(GSResult<AppInfoList>::success(app_list));
            });
        });
}

void GameStreamClient::applist(const Host& host,
                               ServerCallback<AppInfoList>& callback) {
    applist(active_address(host), callback);
}

void GameStreamClient::app_boxart(const std::string& address, int app_id,
                                  ServerCallback<Data>& callback) {
    with_cached_server_data<Data>(
        address, "Firstly call connect() & pair()...", callback,
        [app_id](const std::string& cachedAddress, SERVER_DATA& server,
                 ServerCallback<Data>& callback) {
            Data data;
            int status = gs_app_boxart(&server, app_id, &data);
            const std::string error = status == GS_OK ? "" : gs_error();

            brls::sync([callback, data, status, error] {
                if (status == GS_OK) {
                    callback(GSResult<Data>::success(data));
                } else {
                    callback(GSResult<Data>::failure(error));
                }
            });
        });
}

void GameStreamClient::app_boxart(const Host& host, int app_id,
                                  ServerCallback<Data>& callback) {
    app_boxart(active_address(host), app_id, callback);
}

void GameStreamClient::start(const std::string& address,
                             STREAM_CONFIGURATION config, int app_id,
                             ServerCallback<STREAM_CONFIGURATION>& callback) {
    with_cached_server_data<STREAM_CONFIGURATION>(
        address, "Firstly call connect() & pair()...", callback,
        [this, app_id, config](const std::string& cachedAddress,
                               SERVER_DATA& server,
                               ServerCallback<STREAM_CONFIGURATION>& callback) {
            STREAM_CONFIGURATION started = config;
            int status = gs_start_app(&server, &started, app_id,
                                      Settings::instance().sops(),
                                      Settings::instance().play_audio(), 0x1);
            const std::string error = status == GS_OK ? "" : gs_error();
            if (status == GS_OK)
                cache_server_data(cachedAddress, server);

            brls::sync([callback, status, error, started] {
                if (status == GS_OK) {
                    callback(GSResult<STREAM_CONFIGURATION>::success(started));
                } else {
                    callback(GSResult<STREAM_CONFIGURATION>::failure(error));
                }
            });
        });
}

void GameStreamClient::start(const Host& host, STREAM_CONFIGURATION config,
                             int app_id,
                             ServerCallback<STREAM_CONFIGURATION>& callback) {
    start(active_address(host), config, app_id, callback);
}

void GameStreamClient::quit(const std::string& address,
                            ServerCallback<bool>& callback) {
    with_cached_server_data<bool>(
        address, "Firstly call connect() & pair()...", callback,
        [](const std::string& cachedAddress, SERVER_DATA& server,
           ServerCallback<bool>& callback) {
            int status = gs_quit_app(&server);
            const std::string error = status == GS_OK ? "" : gs_error();

            brls::sync([callback, status, error] {
                if (status == GS_OK) {
                    callback(GSResult<bool>::success(true));
                } else {
                    callback(GSResult<bool>::failure(error));
                }
            });
        });
}

void GameStreamClient::quit(const Host& host,
                            ServerCallback<bool>& callback) {
    quit(active_address(host), callback);
}

void GameStreamClient::steam_import(
    const Host& host, ServerCallback<SteamImportResult>& callback) {
    with_cached_server_data<SteamImportResult>(
        active_address(host), "Firstly call connect() & pair()...", callback,
        [](const std::string& cachedAddress, SERVER_DATA& server,
           ServerCallback<SteamImportResult>& callback) {
            SteamImportResult result;
            int status = gs_steam_import(&server, &result);
            const std::string error = status == GS_OK ? "" : gs_error();

            brls::sync([callback, status, result, error] {
                if (status == GS_OK)
                    callback(GSResult<SteamImportResult>::success(result));
                else if (status == GS_NOT_SUPPORTED)
                    callback(GSResult<SteamImportResult>::failure("not_supported"));
                else
                    callback(GSResult<SteamImportResult>::failure(error));
            });
        });
}

void GameStreamClient::ping(const std::string& address,
                            ServerCallback<float>& callback, bool warmup) {
    with_cached_server_data<float>(
        address, "Firstly call connect() & pair()...", callback,
        [warmup](const std::string& cachedAddress, SERVER_DATA& server,
                 ServerCallback<float>& callback) {
            float rtt = 0.0f;
            int status = gs_ping(&server, &rtt,
                                 warmup ? HTTPRequestTimeoutMedium
                                        : HTTPRequestTimeoutLow);
            const std::string error = status == GS_OK ? "" : gs_error();

            brls::sync([callback, status, rtt, error] {
                if (status == GS_OK) {
                    callback(GSResult<float>::success(rtt));
                } else if (status == GS_NOT_SUPPORTED) {
                    callback(GSResult<float>::failure("not_supported"));
                } else {
                    callback(GSResult<float>::failure(error));
                }
            });
        });
}

// on_progress is invoked on the worker thread (not the UI thread); callers
// must marshal to the UI themselves (e.g. brls::sync).
void GameStreamClient::speedtest(
    const std::string& address, int seconds,
    std::function<void(double elapsed_s, uint64_t bytes)> on_progress,
    ServerCallback<bool>& callback,
    std::shared_ptr<std::atomic<bool>> cancel) {
    with_cached_server_data<bool>(
        address, "Firstly call connect() & pair()...", callback,
        [seconds, on_progress, cancel](const std::string& cachedAddress,
                                       SERVER_DATA& server,
                                       ServerCallback<bool>& callback) {
            int status = gs_speedtest(&server, seconds, on_progress, cancel);
            const std::string error = status == GS_OK ? "" : gs_error();

            brls::sync([callback, status, error] {
                if (status == GS_OK) {
                    callback(GSResult<bool>::success(true));
                } else if (status == GS_NOT_SUPPORTED) {
                    callback(GSResult<bool>::failure("not_supported"));
                } else {
                    callback(GSResult<bool>::failure(error));
                }
            });
        });
}
