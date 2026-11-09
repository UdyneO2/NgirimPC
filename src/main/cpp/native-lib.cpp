#include <jni.h>
#include <string>

extern "C" JNIEXPORT jstring JNICALL
Java_com_ngirim_pc_NativeCore_stringFromJNI(JNIEnv* env, jobject /* this */) {
    std::string hello = "Hello dari Modul C++ Native di PC (Windows Edition)!";
    return env->NewStringUTF(hello.c_str());
}