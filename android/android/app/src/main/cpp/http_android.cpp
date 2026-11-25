// HTTP на Android: соединение открывает Java (com.mixtape.app.Http, там же перенаправления и TLS),
// тело читаем кусками отсюда — так работают прогресс, отмена и предел размера.
#include <stdexcept>

#include "http.hpp"
#include "jni_env.hpp"

namespace mx {

namespace {

std::string friendly(const std::string& javaError) {
    auto has = [&](const char* s) { return javaError.find(s) != std::string::npos; };
    if (has("UnknownHost")) return "Нет связи с сервером: проверьте интернет";
    if (has("Timeout")) return "Сервер не ответил вовремя";
    if (has("SSL") || has("Certificate")) return "Не удалось установить защищённое соединение";
    if (has("ConnectException") || has("NoRouteToHost")) return "Не удалось подключиться к серверу";
    return "Ошибка сети: " + javaError;
}

class AndroidHttp : public IHttp {
public:
    HttpResponse request(const std::string& url, const HttpOptions& opt) override {
        JNIEnv* e = mxa::env();
        jclass hc = mxa::httpClass();
        static jmethodID open = e->GetStaticMethodID(hc, "open", "(Ljava/lang/String;Ljava/lang/String;[B[Ljava/lang/String;I)Lcom/mixtape/app/Http;");
        static jmethodID read = e->GetMethodID(hc, "read", "([B)I");
        static jmethodID close = e->GetMethodID(hc, "close", "()V");
        static jfieldID fStatus = e->GetFieldID(hc, "status", "I");
        static jfieldID fType = e->GetFieldID(hc, "contentType", "Ljava/lang/String;");
        static jfieldID fLen = e->GetFieldID(hc, "length", "J");

        // локальные ссылки освобождаются при выходе из кадра — рабочие потоки живут долго
        e->PushLocalFrame(32);
        struct Pop { JNIEnv* e; ~Pop() { e->PopLocalFrame(nullptr); } } pop{e};

        jclass strc = e->FindClass("java/lang/String");
        jobjectArray hdrs = e->NewObjectArray(static_cast<jsize>(opt.headers.size() * 2), strc, nullptr);
        for (std::size_t i = 0; i < opt.headers.size(); ++i) {
            e->SetObjectArrayElement(hdrs, static_cast<jsize>(i * 2), mxa::jstr(e, opt.headers[i].first));
            e->SetObjectArrayElement(hdrs, static_cast<jsize>(i * 2 + 1), mxa::jstr(e, opt.headers[i].second));
        }
        jbyteArray body = nullptr;
        if (!opt.body.empty()) {
            body = e->NewByteArray(static_cast<jsize>(opt.body.size()));
            e->SetByteArrayRegion(body, 0, static_cast<jsize>(opt.body.size()), reinterpret_cast<const jbyte*>(opt.body.data()));
        }
        jobject h = e->CallStaticObjectMethod(hc, open, mxa::jstr(e, url), mxa::jstr(e, opt.method), body, hdrs,
                                              static_cast<jint>(opt.timeoutMs));
        std::string ex = mxa::takeException(e);
        if (!ex.empty() || !h) throw std::runtime_error(friendly(ex));

        HttpResponse out;
        out.status = e->GetIntField(h, fStatus);
        out.contentType = mxa::str(e, static_cast<jstring>(e->GetObjectField(h, fType)));
        long long len = e->GetLongField(h, fLen);
        std::size_t total = len > 0 ? static_cast<std::size_t>(len) : 0;
        if (total > opt.maxBytes) {
            e->CallVoidMethod(h, close);
            throw std::runtime_error("Файл слишком большой");
        }
        if (total) out.body.reserve(total);

        jbyteArray buf = e->NewByteArray(1 << 16);
        std::string err;
        for (;;) {
            if (opt.cancel && opt.cancel->load()) { err = "Загрузка отменена"; break; }
            jint n = e->CallIntMethod(h, read, buf);
            std::string rex = mxa::takeException(e);
            if (!rex.empty()) { err = friendly(rex); break; }
            if (n < 0) break;
            std::size_t old = out.body.size();
            out.body.resize(old + static_cast<std::size_t>(n));
            e->GetByteArrayRegion(buf, 0, n, reinterpret_cast<jbyte*>(out.body.data() + old));
            if (out.body.size() > opt.maxBytes) { err = "Файл слишком большой"; break; }
            if (opt.progress) opt.progress(out.body.size(), total);
        }
        e->CallVoidMethod(h, close);
        mxa::takeException(e);
        if (!err.empty()) throw std::runtime_error(err);
        return out;
    }
};

}  // namespace

std::unique_ptr<IHttp> makeHttp() { return std::make_unique<AndroidHttp>(); }

}  // namespace mx
