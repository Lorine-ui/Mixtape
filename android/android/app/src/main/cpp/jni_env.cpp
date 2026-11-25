#include "jni_env.hpp"

#include <pthread.h>

namespace mxa {

namespace {
JavaVM* g_vm = nullptr;
jclass g_activity = nullptr, g_http = nullptr;
pthread_key_t g_key;

void detach(void*) {
    if (g_vm) g_vm->DetachCurrentThread();
}

// Java-строки в JNI — «модифицированный UTF-8»: символы вне BMP (эмодзи) идут парами суррогатов.
// Поэтому переводим через String.getBytes("UTF-8") и new String(bytes, "UTF-8").
jclass stringClass(JNIEnv* e) {
    static jclass c = static_cast<jclass>(e->NewGlobalRef(e->FindClass("java/lang/String")));
    return c;
}
}  // namespace

void jniInit(JavaVM* vm, jobject activity) {
    g_vm = vm;
    pthread_key_create(&g_key, detach);
    JNIEnv* e = env();
    jclass act = e->GetObjectClass(activity);
    g_activity = static_cast<jclass>(e->NewGlobalRef(act));
    // загрузчик классов приложения: им же ищем остальные наши классы
    jmethodID getCl = e->GetMethodID(act, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject cl = e->CallObjectMethod(activity, getCl);
    jclass clc = e->FindClass("java/lang/ClassLoader");
    jmethodID load = e->GetMethodID(clc, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name = e->NewStringUTF("com.mixtape.app.Http");
    g_http = static_cast<jclass>(e->NewGlobalRef(e->CallObjectMethod(cl, load, name)));
    e->DeleteLocalRef(name);
    stringClass(e);
}

JNIEnv* env() {
    JNIEnv* e = nullptr;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) == JNI_OK) return e;
    g_vm->AttachCurrentThread(&e, nullptr);
    pthread_setspecific(g_key, e);  // при завершении потока сработает detach
    return e;
}

jclass activityClass() { return g_activity; }
jclass httpClass() { return g_http; }

std::string str(JNIEnv* e, jstring s) {
    if (!s) return {};
    jclass sc = stringClass(e);
    static jmethodID getBytes = e->GetMethodID(sc, "getBytes", "(Ljava/lang/String;)[B");
    jstring cs = e->NewStringUTF("UTF-8");
    auto arr = static_cast<jbyteArray>(e->CallObjectMethod(s, getBytes, cs));
    e->DeleteLocalRef(cs);
    std::string out;
    if (arr) {
        jsize n = e->GetArrayLength(arr);
        out.resize(static_cast<std::size_t>(n));
        e->GetByteArrayRegion(arr, 0, n, reinterpret_cast<jbyte*>(out.data()));
        e->DeleteLocalRef(arr);
    }
    return out;
}

jstring jstr(JNIEnv* e, const std::string& s) {
    jclass sc = stringClass(e);
    static jmethodID ctor = e->GetMethodID(sc, "<init>", "([BLjava/lang/String;)V");
    jbyteArray arr = e->NewByteArray(static_cast<jsize>(s.size()));
    e->SetByteArrayRegion(arr, 0, static_cast<jsize>(s.size()), reinterpret_cast<const jbyte*>(s.data()));
    jstring cs = e->NewStringUTF("UTF-8");
    auto r = static_cast<jstring>(e->NewObject(sc, ctor, arr, cs));
    e->DeleteLocalRef(arr);
    e->DeleteLocalRef(cs);
    return r;
}

std::string takeException(JNIEnv* e) {
    jthrowable ex = e->ExceptionOccurred();
    if (!ex) return {};
    e->ExceptionClear();
    jclass tc = e->FindClass("java/lang/Throwable");
    jmethodID ts = e->GetMethodID(tc, "toString", "()Ljava/lang/String;");
    auto s = static_cast<jstring>(e->CallObjectMethod(ex, ts));
    std::string msg = str(e, s);
    e->DeleteLocalRef(ex);
    e->DeleteLocalRef(tc);
    if (s) e->DeleteLocalRef(s);
    if (e->ExceptionCheck()) e->ExceptionClear();
    return msg.empty() ? "ошибка сети" : msg;
}

}  // namespace mxa
