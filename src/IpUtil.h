#pragma once

// 一些与 IP / 字符串 / 时间有关的小工具，全部 inline，避免额外编译单元。

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace antibang {

inline std::int64_t nowSeconds() {
    using namespace std::chrono;
    return static_cast<std::int64_t>(duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}

inline char toLowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

inline std::string toLower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) {
        c = toLowerAscii(c);
    }
    return out;
}

inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (toLowerAscii(a[i]) != toLowerAscii(b[i])) {
            return false;
        }
    }
    return true;
}

inline std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
        s.remove_suffix(1);
    }
    return s;
}

// BDS 的 getIPAndPort() 在不同版本里可能是 "1.2.3.4:19132"、"1.2.3.4|19132" 或 "[::1]:19132"，
// 这里统一剥掉端口，只留地址。
inline std::string stripPort(std::string_view raw) {
    std::string_view s = trim(raw);
    if (s.empty()) {
        return {};
    }
    if (auto bar = s.find('|'); bar != std::string_view::npos) {
        return std::string(s.substr(0, bar));
    }
    if (s.front() == '[') {
        if (auto bracket = s.find(']'); bracket != std::string_view::npos) {
            return std::string(s.substr(1, bracket - 1));
        }
    }
    if (std::count(s.begin(), s.end(), ':') == 1) {
        return std::string(s.substr(0, s.find(':')));
    }
    return std::string(s);
}

inline bool parseIpv4(std::string_view s, std::uint32_t& out) {
    std::uint32_t value = 0;
    int           parts = 0;
    std::size_t   i     = 0;
    while (i <= s.size()) {
        std::size_t dot = s.find('.', i);
        if (dot == std::string_view::npos) {
            dot = s.size();
        }
        if (dot == i || dot - i > 3) {
            return false;
        }
        std::uint32_t octet = 0;
        for (std::size_t k = i; k < dot; ++k) {
            if (s[k] < '0' || s[k] > '9') {
                return false;
            }
            octet = octet * 10 + static_cast<std::uint32_t>(s[k] - '0');
        }
        if (octet > 255) {
            return false;
        }
        value = (value << 8) | octet;
        ++parts;
        if (dot == s.size()) {
            break;
        }
        i = dot + 1;
    }
    if (parts != 4) {
        return false;
    }
    out = value;
    return true;
}

// 只支持 IPv4 的 CIDR（服务端绝大多数情况够用），IPv6 直接按字符串精确匹配。
inline bool ipv4InCidr(std::string_view ip, std::string_view cidr) {
    auto slash = cidr.find('/');
    if (slash == std::string_view::npos) {
        return false;
    }
    std::uint32_t net  = 0;
    std::uint32_t addr = 0;
    if (!parseIpv4(trim(cidr.substr(0, slash)), net)) {
        return false;
    }
    if (!parseIpv4(ip, addr)) {
        return false;
    }

    std::string_view prefixText = trim(cidr.substr(slash + 1));
    if (prefixText.empty() || prefixText.size() > 2) {
        return false;
    }
    int prefix = 0;
    for (char c : prefixText) {
        if (c < '0' || c > '9') {
            return false;
        }
        prefix = prefix * 10 + (c - '0');
    }
    if (prefix < 0 || prefix > 32) {
        return false;
    }
    if (prefix == 0) {
        return true;
    }

    std::uint32_t mask = (prefix == 32) ? 0xFFFFFFFFu : (0xFFFFFFFFu << (32 - prefix));
    return (addr & mask) == (net & mask);
}

// 把 IPv4 转成 a.b.c.0/24，用于「封一个 IP 时顺手封掉整个 C 段」。
// 非 IPv4 原样返回（调用方仍按精确 IP 处理）。
inline std::string toSlash24(std::string_view ip) {
    std::uint32_t value = 0;
    if (!parseIpv4(ip, value)) {
        return std::string(ip);
    }
    return std::to_string((value >> 24) & 0xFF) + '.' + std::to_string((value >> 16) & 0xFF) + '.'
         + std::to_string((value >> 8) & 0xFF) + ".0/24";
}

inline bool looksLikeIp(std::string_view s) {
    std::uint32_t value = 0;
    if (parseIpv4(s, value)) {
        return true;
    }
    return s.find(':') != std::string_view::npos && std::count(s.begin(), s.end(), ':') >= 2;
}

inline std::vector<std::string_view> splitTabs(std::string_view line) {
    std::vector<std::string_view> parts;
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    std::size_t start = 0;
    while (true) {
        auto pos = line.find('\t', start);
        if (pos == std::string_view::npos) {
            parts.push_back(line.substr(start));
            break;
        }
        parts.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

} // namespace antibang
