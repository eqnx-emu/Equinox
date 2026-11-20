#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "Host.hpp"

namespace {
Host pc(const std::string& mac, const std::string& address, const std::string& remote) {
    Host h;
    h.mac = mac;
    h.address = address;
    h.remoteAddress = remote;
    h.hostname = "PC-" + mac;
    return h;
}
}  // namespace

TEST_CASE("set_host_addresses changes the local address of the PC with that MAC") {
    std::vector<Host> hosts{pc("AA", "192.168.0.10", "home.example.com"), pc("BB", "192.168.0.20", "")};
    CHECK(set_host_addresses(hosts, pc("AA", "", ""), "192.168.0.11", "home.example.com"));
    REQUIRE(hosts.size() == 2);
    CHECK(hosts[0].address == "192.168.0.11");
    CHECK(hosts[0].remoteAddress == "home.example.com");
    CHECK(hosts[0].hostname == "PC-AA");
    CHECK(hosts[1].address == "192.168.0.20");
}

TEST_CASE("set_host_addresses clears the remote address") {
    std::vector<Host> hosts{pc("AA", "192.168.0.10", "home.example.com")};
    CHECK(set_host_addresses(hosts, hosts[0], "192.168.0.10", ""));
    CHECK(hosts[0].remoteAddress.empty());
}

TEST_CASE("set_host_addresses: another PC's address does not create a duplicate") {
    std::vector<Host> hosts{pc("AA", "192.168.0.10", ""), pc("BB", "192.168.0.20", "")};
    CHECK(set_host_addresses(hosts, hosts[0], "192.168.0.20", ""));
    REQUIRE(hosts.size() == 2);
    CHECK(hosts[0].mac == "AA");
    CHECK(hosts[0].address == "192.168.0.20");
    CHECK(hosts[1].mac == "BB");
}

TEST_CASE("set_host_addresses refuses no address at all, or an unknown PC") {
    std::vector<Host> hosts{pc("AA", "192.168.0.10", "home.example.com")};
    CHECK_FALSE(set_host_addresses(hosts, hosts[0], "", ""));
    CHECK(hosts[0].address == "192.168.0.10");
    CHECK_FALSE(set_host_addresses(hosts, pc("CC", "", ""), "10.0.0.1", ""));
    CHECK(hosts.size() == 1);
}

TEST_CASE("set_host_addresses keeps only a remote address") {
    std::vector<Host> hosts{pc("AA", "192.168.0.10", "")};
    CHECK(set_host_addresses(hosts, hosts[0], "", "home.example.com"));
    CHECK(hosts[0].address.empty());
    CHECK(hosts[0].preferred_address() == "home.example.com");
}

TEST_CASE("address_check: same MAC saves, another MAC or no answer asks") {
    CHECK(address_check("AA", {}) == AddressCheck::Same);  // nothing new to check
    CHECK(address_check("AA", {{"192.168.0.11", true, "AA"}}) == AddressCheck::Same);
    CHECK(address_check("AA", {{"192.168.0.11", true, "BB"}}) == AddressCheck::OtherPc);
    CHECK(address_check("AA", {{"192.168.0.11", false, ""}}) == AddressCheck::NoAnswer);
    CHECK(address_check("AA", {{"192.168.0.11", true, ""}}) == AddressCheck::NoAnswer);  // no MAC to compare
    // Another PC wins over no answer: it is the stronger warning.
    CHECK(address_check("AA", {{"192.168.0.11", false, ""}, {"home.example.com", true, "BB"}}) ==
          AddressCheck::OtherPc);
    CHECK(address_check("AA", {{"192.168.0.11", true, "AA"}, {"home.example.com", false, ""}}) ==
          AddressCheck::NoAnswer);
}

TEST_CASE("address_check: a PC saved without a MAC is asked about, not called another PC") {
    CHECK(address_check("", {{"192.168.0.11", true, "AA"}}) == AddressCheck::NoAnswer);
    CHECK(address_check("", {{"192.168.0.11", false, ""}}) == AddressCheck::NoAnswer);
}
