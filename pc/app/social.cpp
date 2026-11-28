// Профиль и друзья. Всё хранится на этом устройстве (profile.json, friends.json), сервера нет:
// друг добавляется ссылкой-приглашением, песня рекомендуется ссылкой на неё (см. share.hpp).
#include <algorithm>
#include <chrono>
#include <filesystem>

#include "app.hpp"
#include "text.hpp"

namespace mx {

namespace fs = std::filesystem;

namespace {

fs::path P(const std::string& s) { return fs::u8path(s); }

long long nowMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace

void App::socialLoad() {
    std::string text;
    Json j;
    if (readFile((P(dataDir) / "profile.json").u8string(), text) && Json::tryParse(text, j)) {
        acct.prof = Profile::fromJson(j);
    } else if (readFile((P(dataDir) / "account.json").u8string(), text) && Json::tryParse(text, j)) {
        // профиль из прежних версий, где был вход в аккаунт: переносим себе, а сам файл (в нём ключ входа) удаляем
        acct.prof = Profile::fromJson(j["profile"]);
        std::error_code ec;
        fs::remove(P(dataDir) / "account.json", ec);
        fs::remove(P(dataDir) / "account.json.bak", ec);
        profileChanged();
    }
    ++acct.avatarVer;

    if (readFile((P(dataDir) / "friends.json").u8string(), text) && Json::tryParse(text, j)) {
        myId_ = j["me"].asStr();
        for (const auto& f : j["friends"].items()) {
            FriendContact c;
            c.id = f["id"].asStr();
            c.name = f["name"].asStr();
            c.status = f["status"].asStr();
            c.color = std::clamp(static_cast<int>(f["color"].asNum(0)), 0, 7);
            c.frame = std::max(0, static_cast<int>(f["frame"].asNum(1)));
            c.added = static_cast<long long>(f["added"].asNum());
            if (!c.id.empty() && !c.name.empty()) friendList.push_back(std::move(c));
        }
        for (const auto& r : j["inbox"].items()) {
            Recommendation rec;
            rec.key = r["key"].asStr();
            rec.fromName = r["from"].asStr();
            rec.note = r["note"].asStr();
            rec.t = static_cast<long long>(r["t"].asNum());
            rec.track = trackFromJson(r["track"]);
            if (!rec.key.empty() && !rec.track.id.empty()) inbox.push_back(std::move(rec));
        }
    }
    if (myId_.empty()) { myId_ = newId(); socialSave(); }
}

void App::socialSave() {
    Json j = Json::object();
    j.set("me", Json(myId_));
    Json fl = Json::array();
    for (const auto& c : friendList) {
        Json f = Json::object();
        f.set("id", Json(c.id));
        f.set("name", Json(c.name));
        f.set("status", Json(c.status));
        f.set("color", Json(c.color));
        f.set("frame", Json(c.frame));
        f.set("added", Json(c.added));
        fl.push(f);
    }
    j.set("friends", fl);
    Json in = Json::array();
    for (const auto& r : inbox) {
        Json o = Json::object();
        o.set("key", Json(r.key));
        o.set("from", Json(r.fromName));
        o.set("note", Json(r.note));
        o.set("t", Json(r.t));
        o.set("track", toJson(r.track));
        in.push(o);
    }
    j.set("inbox", in);
    writeFileAtomic((P(dataDir) / "friends.json").u8string(), j.dump());
    writeFileAtomic((P(dataDir) / "profile.json").u8string(), acct.prof.toJson().dump());
}

void App::profileChanged() {
    acct.prof.updated = nowMillis();
    socialSave();
}

void App::setAvatar(const std::string& b64) {
    acct.prof.avatar = b64;
    ++acct.avatarVer;
    profileChanged();
}

void App::openFriends() {
    view = ViewKind::Friends;
    viewPlaylist.clear();
}

std::string App::myFriendLink() const {
    ShareFriend f;
    f.id = myId_;
    f.name = acct.name();
    f.status = acct.prof.status;
    f.color = acct.prof.color;
    f.frame = acct.prof.frame;
    return makeFriendLink(f);
}

bool App::handleShare(const std::string& text) {
    ShareParsed p = parseShareText(text);
    if (p.kind == ShareKind::None) return false;
    if (p.kind == ShareKind::Friend) {
        if (p.person.id == myId_) { toast("Это ваша собственная ссылка. Отправьте её другу"); return true; }
        for (auto& c : friendList)
            if (c.id == p.person.id) {
                c.name = p.person.name;
                c.status = p.person.status;
                c.color = p.person.color;
                c.frame = p.person.frame;
                socialSave();
                toast("Данные друга обновлены: " + c.name);
                return true;
            }
        FriendContact c;
        c.id = p.person.id;
        c.name = p.person.name;
        c.status = p.person.status;
        c.color = p.person.color;
        c.frame = p.person.frame;
        c.added = nowMillis();
        friendList.push_back(std::move(c));
        socialSave();
        toast("Друг добавлен: " + p.person.name + ". Отправьте ему и свою ссылку, чтобы он добавил вас", 7.0);
        return true;
    }
    if (p.song.fromId == myId_) { toast("Это ссылка на песню от вас самих"); return true; }
    const std::string key = p.song.fromId + "|" + p.song.track.id;
    for (const auto& r : inbox)
        if (r.key == key) { toast("Эта рекомендация уже есть в списке"); return true; }
    Recommendation r;
    r.key = key;
    r.fromName = p.song.fromName.empty() ? std::string("Друг") : p.song.fromName;
    for (const auto& f : friendList) if (f.id == p.song.fromId) r.fromName = f.name;
    r.note = p.song.note;
    r.track = p.song.track;
    r.t = nowMillis();
    inbox.insert(inbox.begin(), r);
    socialSave();
    toast(r.fromName + " рекомендует: " + r.track.title, 6.0);
    return true;
}

void App::shareSong(const Track& t) {
    if (t.source == "local" || t.clip) { toast("Эту песню нельзя отправить: она есть только на этом устройстве"); return; }
    ShareSong s;
    s.track = t;
    s.fromId = myId_;
    s.fromName = acct.name();
    if (platform.copyText) platform.copyText(makeSongLink(s));
    toast("Ссылка скопирована: вставьте её другу в чат", 5.0);
}

void App::friendRemove(const std::string& id) {
    friendList.erase(std::remove_if(friendList.begin(), friendList.end(), [&](const FriendContact& c) { return c.id == id; }), friendList.end());
    socialSave();
    toast("Убрали из друзей");
}

void App::recPlay(const Recommendation& r) {
    if (!lib.has(r.track.id)) cache_[r.track.id] = r.track;
    playFrom({r.track.id}, r.track.id);
}

void App::recDismiss(const std::string& key) {
    inbox.erase(std::remove_if(inbox.begin(), inbox.end(), [&](const Recommendation& r) { return r.key == key; }), inbox.end());
    socialSave();
}

void App::recSave(const Recommendation& r) {
    if (!lib.has(r.track.id)) {
        Track t = r.track;
        t.added = nowMillis();
        lib.add(t);
        touch();
    }
    toast("Добавлено в «Мою музыку»: " + r.track.title);
    recDismiss(r.key);
}

}  // namespace mx
