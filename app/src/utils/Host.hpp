#pragma once

#include <algorithm>
#include <string>
#include <vector>

struct App {
    std::string name;
    int app_id;
};

struct Host {
    std::string address;
    std::string remoteAddress;
    std::string hostname;
    std::string mac;
    std::vector<App> favorites;

    [[nodiscard]] std::vector<std::string> connection_addresses() const {
        std::vector<std::string> addresses;
        if (!address.empty())
            addresses.push_back(address);
        if (!remoteAddress.empty() && remoteAddress != address)
            addresses.push_back(remoteAddress);
        return addresses;
    }

    [[nodiscard]] std::string preferred_address() const {
        return !address.empty() ? address : remoteAddress;
    }

    [[nodiscard]] bool has_address(const std::string& value) const {
        return !value.empty() &&
               (address == value || remoteAddress == value);
    }
};

inline bool hosts_match(const Host& lhs, const Host& rhs) {
    if (!lhs.mac.empty() && !rhs.mac.empty())
        return lhs.mac == rhs.mac;

    for (const auto& address : lhs.connection_addresses()) {
        if (rhs.has_address(address))
            return true;
    }

    return false;
}

// Sets both addresses of the saved PC that matches `host`; an empty one is
// cleared. False, with nothing changed, when no saved PC matches or both
// addresses would be empty.
inline bool set_host_addresses(std::vector<Host>& hosts, const Host& host,
                               const std::string& address,
                               const std::string& remoteAddress) {
    if (address.empty() && remoteAddress.empty())
        return false;
    auto it = std::find_if(hosts.begin(), hosts.end(), [&](const Host& saved) {
        return hosts_match(saved, host);
    });
    if (it == hosts.end())
        return false;
    it->address = address;
    it->remoteAddress = remoteAddress;
    return true;
}

// What a new address answered when asked for its serverinfo.
struct AddressProbe {
    std::string address;
    bool answered = false;
    std::string mac;
};

enum class AddressCheck { Same, OtherPc, NoAnswer };

// Same when every probed address answered with the saved PC's MAC. A
// different MAC anywhere is OtherPc; otherwise a silent address, or one
// without a MAC to compare, is NoAnswer.
inline AddressCheck address_check(const std::string& savedMac,
                                  const std::vector<AddressProbe>& probes) {
    AddressCheck result = AddressCheck::Same;
    for (const AddressProbe& p : probes) {
        if (p.answered && !p.mac.empty() && p.mac != savedMac)
            return AddressCheck::OtherPc;
        if (!p.answered || p.mac.empty())
            result = AddressCheck::NoAnswer;
    }
    return result;
}
