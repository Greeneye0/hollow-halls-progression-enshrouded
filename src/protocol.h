#pragma once
// Hidden chat lines between a remote player and the host (dedicated server or the player hosting).
// They travel as chat entries of type 255: every server relays them unchanged and the chat window
// never draws them. A host replaces a question with its answer, which comes back to every player
// with the asker as its sender; each player keeps only the answers sent as itself.
#include "halls.h"
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace hhp::protocol {

inline constexpr uint8_t kChatType = 255;
// Sent by a remote player after joining (twice at most); a host with the mod answers kHelloReply.
inline constexpr std::string_view kHello = "/hhh1", kHelloReply = "/hhs1 hello";
// The asker's unlocked beacons: "/hhu1 <reset> <mask hall 0> ... <mask hall 5>", bit n = beacon n+1.
inline constexpr std::string_view kQuery = "/hhq1";
// Travel to a beacon: "/hht1 <nonce> <hall> <beacon>" (beacon from 0), answered "/hhr1 <nonce> <code>".
enum class Answer : uint8_t { queued, locked, busy, unavailable };

struct Masks {
    uint8_t hall[halls::kHallCount]{};
    bool resetOnBoss = false;
};

namespace detail {
template <class T>
inline bool Next(std::string_view& text, T& out, bool last) {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    auto r = std::from_chars(text.data(), text.data() + text.size(), out);
    if (r.ec != std::errc{}) return false;
    text.remove_prefix(size_t(r.ptr - text.data()));
    if (last) return text.empty();
    return !text.empty() && text.front() == ' ';
}
}  // namespace detail

inline int FormatMasks(char* out, size_t capacity, const Masks& m) {
    int n = sprintf_s(out, capacity, "/hhu1 %u", m.resetOnBoss ? 1u : 0u);
    for (int h = 0; n > 0 && h < halls::kHallCount; ++h) {
        int more = sprintf_s(out + n, capacity - size_t(n), " %u", unsigned(m.hall[h]));
        n = more > 0 ? n + more : -1;
    }
    return n;
}

inline bool ParseMasks(std::string_view text, Masks& out) {
    if (!text.starts_with("/hhu1 ") || text.size() > 96) return false;
    text.remove_prefix(6);
    Masks m{};
    unsigned value = 0;
    if (!detail::Next(text, value, false) || value > 1) return false;
    m.resetOnBoss = value != 0;
    for (int h = 0; h < halls::kHallCount; ++h) {
        if (!detail::Next(text, value, h + 1 == halls::kHallCount) || value > 0xff) return false;
        m.hall[h] = uint8_t(value);
    }
    out = m;
    return true;
}

struct Travel { uint64_t nonce{}; int hall{}, beacon{}; };

inline int FormatTravel(char* out, size_t capacity, const Travel& t) {
    return sprintf_s(out, capacity, "/hht1 %llu %d %d", static_cast<unsigned long long>(t.nonce), t.hall, t.beacon);
}

inline bool ParseTravel(std::string_view text, Travel& out) {
    if (!text.starts_with("/hht1 ") || text.size() > 64) return false;
    text.remove_prefix(6);
    Travel t{};
    if (!detail::Next(text, t.nonce, false) || !detail::Next(text, t.hall, false) || !detail::Next(text, t.beacon, true))
        return false;
    if (!t.nonce || t.hall < 0 || t.hall >= halls::kHallCount || t.beacon < 0 || t.beacon >= halls::kHalls[t.hall].count)
        return false;
    out = t;
    return true;
}

inline int FormatAnswer(char* out, size_t capacity, uint64_t nonce, Answer a) {
    return sprintf_s(out, capacity, "/hhr1 %llu %u", static_cast<unsigned long long>(nonce), unsigned(a));
}

inline bool ParseAnswer(std::string_view text, uint64_t& nonce, Answer& a) {
    if (!text.starts_with("/hhr1 ") || text.size() > 48) return false;
    text.remove_prefix(6);
    unsigned code = 0;
    if (!detail::Next(text, nonce, false) || !detail::Next(text, code, true) || code > unsigned(Answer::unavailable))
        return false;
    a = Answer(code);
    return true;
}

}  // namespace hhp::protocol
