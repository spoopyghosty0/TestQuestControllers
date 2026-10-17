#pragma once

#ifdef __ANDROID__
#include <android/log.h>

#define LOG_TAG "TQC"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else  // host builds (tools/render_host)
#include <cstdio>
#define LOGI(...) (std::printf("TQC: " __VA_ARGS__), std::printf("\n"))
#define LOGW LOGI
#define LOGE LOGI
#endif
