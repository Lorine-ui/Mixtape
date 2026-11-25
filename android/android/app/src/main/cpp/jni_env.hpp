// JNI для любого потока: рабочие потоки приложения подключаются к Java сами и отключаются при завершении.
// Классы приложения ищутся один раз из главного потока (из рабочих FindClass их не видит) и хранятся здесь.
#pragma once
#include <jni.h>

#include <string>
#include <vector>

namespace mxa {

void jniInit(JavaVM* vm, jobject activity);  // из android_main, до запуска рабочих потоков
JNIEnv* env();                               // для текущего потока (подключает при первом вызове)
jclass activityClass();                      // com.mixtape.app.MainActivity
jclass httpClass();                          // com.mixtape.app.Http

std::string str(JNIEnv* e, jstring s);       // UTF-8 (пусто для null)
jstring jstr(JNIEnv* e, const std::string& s);
// Текст исключения Java (если было) и сброс его; пусто, если исключения нет.
std::string takeException(JNIEnv* e);

}  // namespace mxa
