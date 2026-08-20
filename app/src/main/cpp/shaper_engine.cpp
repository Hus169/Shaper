#include <jni.h>
#include <android/log.h>
#define LOG_TAG "AxiomShaper"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

extern "C" JNIEXPORT void JNICALL
Java_com_axiom_shaper_ShaperVpnService_startShaper(JNIEnv* env, jobject, jint fd, jlong kbps_limit) {
    LOGI("Local TUN Shaper engaged (FD: %d, Limit: %lld).", fd, (long long)kbps_limit);
}
