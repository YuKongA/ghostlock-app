#pragma once
#include <jni.h>

struct Reporter { JNIEnv *env; jobject obj; };

void reportfmt(struct Reporter *r, const char *fmt, ...) __attribute__((__format__(printf, 2, 3)));
#define REPORTLN(fmt, ...) reportfmt(reporter, fmt "\n" __VA_OPT__(,) __VA_ARGS__)
