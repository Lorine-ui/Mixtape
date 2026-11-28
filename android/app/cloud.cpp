// Аккаунт: вход и регистрация через Firebase Authentication, библиотека хранится в Firebase Realtime Database
// (users/<id>: { updated: время в мс, data: библиотека строкой JSON }).
// Синхронизация: после изменений — отправка через несколько секунд; при запуске и раз в пару минут — проверка облака.
#include <cstdlib>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

#include "app.hpp"
#include "cloud_config.hpp"
#include "cloudsync.hpp"
#include "text.hpp"

namespace mx {

namespace fs = std::filesystem;

namespace {

fs::path P(const std::string& s) { return fs::u8path(s); }

// понятный текст для ошибок Firebase («EMAIL_EXISTS», «INVALID_PASSWORD : ...»)
std::string authError(const std::string& code) {
    auto has = [&](const char* s) { return code.find(s) != std::string::npos; };
    if (has("EMAIL_EXISTS")) return "Такая почта уже зарегистрирована — войдите";
    if (has("EMAIL_NOT_FOUND") || has("INVALID_PASSWORD") || has("INVALID_LOGIN_CREDENTIALS")) return "Неверная почта или пароль";
    if (has("WEAK_PASSWORD")) return "Слишком простой пароль: нужно хотя бы 6 символов";
    if (has("INVALID_EMAIL")) return "Похоже, в адресе почты опечатка";
    if (has("MISSING_PASSWORD")) return "Введите пароль";
    if (has("TOO_MANY_ATTEMPTS")) return "Слишком много попыток. Подождите немного";
    if (has("USER_DISABLED")) return "Этот аккаунт отключён";
    if (has("TOKEN_EXPIRED") || has("INVALID_REFRESH_TOKEN")) return "Вход устарел — войдите заново";
    if (has("OPERATION_NOT_ALLOWED")) return "Вход по почте не включён в проекте Firebase";
    if (has("API_KEY") || has("API key")) return "Неверный ключ проекта Firebase";
    return "Ошибка сервера: " + code;
}

std::string jsonEscape(const std::string& s) { return Json(s).dump(); }

}  // namespace

bool App::cloudConfigured() const { return kFirebaseApiKey[0] != 0 && kFirebaseDbUrl[0] != 0; }

void App::cloudLoadState() {
    std::string text;
    Json j;
    if (!readFile((P(dataDir) / "account.json").u8string(), text) || !Json::tryParse(text, j)) return;
    acct.email = j["email"].asStr();
    acct.uid = j["uid"].asStr();
    acct.refresh = j["refresh"].asStr();
    acct.syncedAt = static_cast<long long>(j["syncedAt"].asNum());
    acct.dirty = j["dirty"].asBool(false);
    acct.prof = Profile::fromJson(j["profile"]);
    acct.profDirty = j["profDirty"].asBool(false);
    ++acct.avatarVer;
}

void App::cloudSaveState() {
    Json j = Json::object();
    j.set("email", Json(acct.email));
    j.set("uid", Json(acct.uid));
    j.set("refresh", Json(acct.refresh));
    j.set("syncedAt", Json(static_cast<double>(acct.syncedAt)));
    j.set("dirty", Json(acct.dirty));
    j.set("profile", acct.prof.toJson());
    j.set("profDirty", Json(acct.profDirty));
    writeFileAtomic((P(dataDir) / "account.json").u8string(), j.dump());
}

// POST в Firebase Auth; бросает исключение с понятным текстом
Json App::cloudAuthPost(const std::string& url, const std::string& body, const std::string& contentType) {
    HttpOptions o;
    o.method = "POST";
    o.body = body;
    o.headers = {{"Content-Type", contentType}};
    o.timeoutMs = 15000;
    HttpResponse r = http_->request(url, o);
    Json j;
    Json::tryParse(r.body, j);
    if (!r.ok()) {
        std::string code = j["error"]["message"].asStr();
        throw std::runtime_error(authError(code.empty() ? "код " + std::to_string(r.status) : code));
    }
    return j;
}

// Действующий ключ входа (живёт час): при необходимости обновляем по refresh-токену. Только из рабочего потока.
std::string App::cloudToken(std::string refresh) {
    {
        std::lock_guard<std::mutex> lk(acctMu_);
        if (!idToken_.empty() && nowMs() < idTokenExp_) return idToken_;
    }
    if (refresh.empty()) throw std::runtime_error("Войдите в аккаунт");
    Json j = cloudAuthPost(std::string("https://securetoken.googleapis.com/v1/token?key=") + kFirebaseApiKey,
                           "grant_type=refresh_token&refresh_token=" + urlEncode(refresh), "application/x-www-form-urlencoded");
    std::string tok = j["id_token"].asStr();
    if (tok.empty()) throw std::runtime_error("Вход устарел — войдите заново");
    long long ttl = static_cast<long long>(std::atof(j["expires_in"].asStr("3600").c_str()));
    std::lock_guard<std::mutex> lk(acctMu_);
    idToken_ = tok;
    idTokenExp_ = nowMs() + (ttl - 120) * 1000;
    return tok;
}

void App::cloudSignIn(const std::string& email, const std::string& password, bool create) {
    if (!cloudConfigured() || acct.busy) return;
    std::string em = trim(email);
    if (em.empty() || password.empty()) { acct.error = "Введите почту и пароль"; return; }
    acct.busy = true;
    acct.error.clear();
    acct.status = create ? "Создаю аккаунт…" : "Вхожу…";
    async([this, em, password, create] {
        std::string err, uid, refresh, idTok;
        long long ttl = 3600;
        try {
            std::string url = std::string("https://identitytoolkit.googleapis.com/v1/accounts:") +
                              (create ? "signUp" : "signInWithPassword") + "?key=" + kFirebaseApiKey;
            Json j = cloudAuthPost(url, "{\"email\":" + jsonEscape(em) + ",\"password\":" + jsonEscape(password) +
                                            ",\"returnSecureToken\":true}", "application/json");
            uid = j["localId"].asStr();
            refresh = j["refreshToken"].asStr();
            idTok = j["idToken"].asStr();
            ttl = static_cast<long long>(std::atof(j["expiresIn"].asStr("3600").c_str()));
            if (uid.empty() || refresh.empty()) throw std::runtime_error("Сервер не вернул данные входа");
        } catch (const std::exception& e) {
            err = e.what();
        }
        post([this, em, uid, refresh, idTok, ttl, err, create] {
            acct.busy = false;
            acct.status.clear();
            if (!err.empty()) { acct.error = err; return; }
            {
                std::lock_guard<std::mutex> lk(acctMu_);
                idToken_ = idTok;
                idTokenExp_ = nowMs() + (ttl - 120) * 1000;
            }
            acct.email = em;
            acct.uid = uid;
            acct.refresh = refresh;
            acct.prof = Profile();  // профиль этого аккаунта придёт из облака
            acct.profDirty = false;
            ++acct.avatarVer;
            acct.syncedAt = 0;    // первая синхронизация с этим аккаунтом: объединяем, ничего не теряя
            acct.dirty = true;
            cloudSaveState();
            toast(create ? "Аккаунт создан: " + em : "Вы вошли: " + em);
            cloudSync(true);
        });
    });
}

void App::cloudSignOut() {
    int av = acct.avatarVer;
    acct = AccountState();
    acct.avatarVer = av + 1;
    {
        std::lock_guard<std::mutex> lk(acctMu_);
        idToken_.clear();
        idTokenExp_ = 0;
    }
    std::error_code ec;
    fs::remove(P(dataDir) / "account.json", ec);
    toast("Вы вышли из аккаунта. Музыка на этом устройстве осталась");
}

void App::cloudResetPassword(const std::string& email) {
    if (!cloudConfigured() || trim(email).empty()) { acct.error = "Введите почту, на неё придёт ссылка"; return; }
    std::string em = trim(email);
    async([this, em] {
        std::string err;
        try {
            cloudAuthPost(std::string("https://identitytoolkit.googleapis.com/v1/accounts:sendOobCode?key=") + kFirebaseApiKey,
                          "{\"requestType\":\"PASSWORD_RESET\",\"email\":" + jsonEscape(em) + "}", "application/json");
        } catch (const std::exception& e) {
            err = e.what();
        }
        post([this, err, em] {
            if (err.empty()) toast("Письмо для смены пароля отправлено на " + em);
            else acct.error = err;
        });
    });
}

// Синхронизация: забираем облако; если там новее — применяем (или объединяем, если и здесь были изменения);
// затем, если нужно, отправляем свою версию.
void App::cloudSync(bool userAsked) {
    if (!cloudConfigured() || acct.uid.empty() || acct.syncing || !online) return;
    acct.syncing = true;
    acct.lastTry = now();
    std::string uid = acct.uid, refresh = acct.refresh;
    long long syncedAt = acct.syncedAt;
    std::string url = std::string(kFirebaseDbUrl) + "/users/" + uid + ".json?auth=";
    async([this, uid, refresh, syncedAt, url, userAsked] {
        std::string err, cloudData;
        long long cloudUpdated = 0;
        Json cloudProf;
        try {
            std::string tok = cloudToken(refresh);
            HttpOptions o;
            o.timeoutMs = 20000;
            HttpResponse r = http_->request(url + urlEncode(tok), o);
            if (r.status == 401) throw std::runtime_error("Нет доступа к данным: проверьте правила базы в Firebase");
            if (!r.ok()) throw std::runtime_error("Сервер ответил ошибкой " + std::to_string(r.status));
            Json j;
            if (Json::tryParse(r.body, j) && j.isObject()) {
                cloudUpdated = static_cast<long long>(j["updated"].asNum());
                cloudData = j["data"].asStr();
                cloudProf = j["profile"];
            }
        } catch (const std::exception& e) {
            err = e.what();
        }
        post([this, err, cloudData, cloudUpdated, cloudProf, syncedAt, userAsked, url, refresh] {
            if (!err.empty()) {
                acct.syncing = false;
                acct.error = err;
                if (userAsked) toast("Синхронизация не удалась: " + err);
                return;
            }
            // профиль: в облаке новее и здесь не правили — берём облачный
            if (cloudProf.isObject()) {
                Profile cp = Profile::fromJson(cloudProf);
                if (cp.updated > acct.prof.updated && !acct.profDirty) {
                    if (cp.avatar != acct.prof.avatar) ++acct.avatarVer;
                    acct.prof = cp;
                }
            } else if (acct.prof.updated > 0) {
                acct.profDirty = true;  // в облаке профиля нет — отправим свой
            }
            bool changed = false;
            if (!cloudData.empty() && cloudUpdated > syncedAt) {
                Json cj;
                if (Json::tryParse(cloudData, cj) && applyCloud(lib, cj, acct.dirty || syncedAt == 0)) {
                    changed = true;
                    dirty_ = true;          // сохранить на диск
                    lastChange_ = now() - 10;
                    refreshSegment();
                }
                acct.syncedAt = cloudUpdated;
            }
            bool needPush = acct.dirty || cloudData.empty() || (changed && (syncedAt == 0));
            if (!needPush) {
                acct.syncing = false;
                acct.error.clear();
                acct.lastOk = now();
                cloudSaveState();
                if (changed) toast("Музыка обновлена из аккаунта");
                return;
            }
            // отправляем свою (уже объединённую) версию
            long long stamp = nowMs();
            std::string body = "{\"updated\":" + std::to_string(stamp) + ",\"data\":" + jsonEscape(cloudPayload(lib).dump()) + "}";
            acct.dirty = false;  // изменения, сделанные пока идёт отправка, снова поставят флаг
            async([this, body, stamp, url, refresh, changed] {
                std::string perr;
                try {
                    std::string tok = cloudToken(refresh);
                    HttpOptions o;
                    o.method = "POST";
                    o.body = body;
                    o.headers = {{"Content-Type", "application/json"}, {"X-HTTP-Method-Override", "PATCH"}};
                    o.timeoutMs = 30000;
                    HttpResponse r = http_->request(url + urlEncode(tok), o);
                    if (r.status == 401) throw std::runtime_error("Нет доступа к данным: проверьте правила базы в Firebase");
                    if (!r.ok()) throw std::runtime_error("Сервер ответил ошибкой " + std::to_string(r.status));
                } catch (const std::exception& e) {
                    perr = e.what();
                }
                post([this, perr, stamp, changed] {
                    acct.syncing = false;
                    if (!perr.empty()) {
                        acct.dirty = true;
                        acct.error = perr;
                        cloudSaveState();
                        return;
                    }
                    acct.syncedAt = stamp;
                    acct.error.clear();
                    acct.lastOk = now();
                    cloudSaveState();
                    if (changed) toast("Музыка обновлена из аккаунта");
                });
            });
        });
    });
}

// раз в кадр: отправка после изменений (через 4 секунды тишины) и проверка облака раз в 2 минуты
void App::cloudTick() {
    if (!cloudConfigured() || acct.uid.empty() || !online) return;
    double t = now();
    if (acct.profDirty && !acct.profSyncing && t - acct.profEditAt > 2 && t - acct.lastTry > 3) cloudPushProfile();
    if (acct.syncing) return;
    if (acct.dirty && t - lastChange_ > 4 && t - acct.lastTry > 8) { cloudSync(false); return; }
    if (t - acct.lastTry > 120) cloudSync(false);
}

// ---------- профиль ----------

void App::profileChanged() {
    acct.prof.updated = nowMs();
    acct.profDirty = true;
    acct.profEditAt = now();
    cloudSaveState();
}

void App::setAvatar(const std::string& b64) {
    acct.prof.avatar = b64;
    ++acct.avatarVer;
    profileChanged();
}

void App::cloudPushProfile() {
    if (!cloudConfigured() || acct.uid.empty() || acct.profSyncing) return;
    acct.profSyncing = true;
    acct.lastTry = now();
    std::string url = std::string(kFirebaseDbUrl) + "/users/" + acct.uid + ".json?auth=", refresh = acct.refresh;
    long long stamp = acct.prof.updated;
    std::string body = "{\"profile\":" + acct.prof.toJson().dump() + "}";
    async([this, url, refresh, body, stamp] {
        std::string err;
        try {
            std::string tok = cloudToken(refresh);
            HttpOptions o;
            o.method = "POST";
            o.body = body;
            o.headers = {{"Content-Type", "application/json"}, {"X-HTTP-Method-Override", "PATCH"}};
            o.timeoutMs = 30000;
            HttpResponse r = http_->request(url + urlEncode(tok), o);
            if (r.status == 401) throw std::runtime_error("Нет доступа к данным: проверьте правила базы в Firebase");
            if (!r.ok()) throw std::runtime_error("Сервер ответил ошибкой " + std::to_string(r.status));
        } catch (const std::exception& e) {
            err = e.what();
        }
        post([this, err, stamp] {
            acct.profSyncing = false;
            if (!err.empty()) { acct.error = err; acct.profEditAt = now() + 20; return; }  // повторим чуть позже
            if (acct.prof.updated == stamp) acct.profDirty = false;  // правили ещё раз, пока шла отправка — отправим снова
            acct.error.clear();
            acct.lastOk = now();
            cloudSaveState();
        });
    });
}

}  // namespace mx
