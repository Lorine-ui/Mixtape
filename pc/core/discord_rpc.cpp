#include "discord_rpc.hpp"

#include <algorithm>

#include "json.hpp"

namespace mx {

namespace {

// Длина UTF-8 символа по первому байту.
std::size_t u8len(unsigned char c) { return c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; }

}  // namespace

std::string clipForDiscord(const std::string& s, std::size_t maxChars) {
    std::string out;
    std::size_t chars = 0, i = 0;
    while (i < s.size() && chars < maxChars) {
        std::size_t n = std::min(u8len(static_cast<unsigned char>(s[i])), s.size() - i);
        out.append(s, i, n);
        i += n;
        ++chars;
    }
    if (i < s.size()) {  // обрезали: последний символ заменяем многоточием
        std::size_t k = out.size();
        while (k > 0 && (static_cast<unsigned char>(out[k - 1]) & 0xC0) == 0x80) --k;
        if (k > 0) out.resize(k - 1);
        out += "\xE2\x80\xA6";
    }
    for (; chars < 2; ++chars) out += "\xE2\xA0\x80";  // невидимые символы: Discord не принимает строки короче двух символов
    return out;
}

std::string discordFrame(std::uint32_t op, const std::string& json) {
    std::string f(8, '\0');
    const std::uint32_t len = static_cast<std::uint32_t>(json.size());
    for (int i = 0; i < 4; ++i) {
        f[static_cast<std::size_t>(i)] = static_cast<char>((op >> (8 * i)) & 0xFF);
        f[static_cast<std::size_t>(4 + i)] = static_cast<char>((len >> (8 * i)) & 0xFF);
    }
    return f + json;
}

std::string discordHandshake(const std::string& appId) {
    Json j = Json::object();
    j.set("v", Json(1));
    j.set("client_id", Json(appId));
    return j.dump();
}

std::string discordSetActivity(const DiscordActivity& a, long pid, const std::string& nonce) {
    Json act = Json::object();
    act.set("type", Json(2));  // «Слушает», а не «Играет»
    act.set("details", Json(clipForDiscord(a.title)));
    act.set("state", Json(clipForDiscord(a.artist.empty() ? std::string("Mixtape") : a.artist)));
    if (a.startSec > 0) {
        Json ts = Json::object();
        ts.set("start", Json(a.startSec));
        if (a.endSec > a.startSec) ts.set("end", Json(a.endSec));
        act.set("timestamps", std::move(ts));
    }
    if (!a.coverUrl.empty() && a.coverUrl.size() <= 256 && a.coverUrl.rfind("https://", 0) == 0) {
        Json as = Json::object();
        as.set("large_image", Json(a.coverUrl));
        as.set("large_text", Json("Mixtape"));
        act.set("assets", std::move(as));
    }
    if (!a.buttonLabel.empty() && a.buttonUrl.rfind("https://", 0) == 0) {
        Json b = Json::object();
        b.set("label", Json(clipForDiscord(a.buttonLabel, 32)));
        b.set("url", Json(a.buttonUrl));
        Json arr = Json::array();
        arr.push(std::move(b));
        act.set("buttons", std::move(arr));
    }
    Json args = Json::object();
    args.set("pid", Json(static_cast<long long>(pid)));
    args.set("activity", std::move(act));
    Json cmd = Json::object();
    cmd.set("cmd", Json("SET_ACTIVITY"));
    cmd.set("args", std::move(args));
    cmd.set("nonce", Json(nonce));
    return cmd.dump();
}

std::string discordClearActivity(long pid, const std::string& nonce) {
    Json args = Json::object();
    args.set("pid", Json(static_cast<long long>(pid)));  // без поля activity Discord убирает статус
    Json cmd = Json::object();
    cmd.set("cmd", Json("SET_ACTIVITY"));
    cmd.set("args", std::move(args));
    cmd.set("nonce", Json(nonce));
    return cmd.dump();
}

}  // namespace mx
