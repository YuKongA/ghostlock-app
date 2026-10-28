#pragma once
#define PROP_VALUE_MAX 92
static inline int __system_property_get(const char *name, char *value) { (void)name; if (value) value[0]=0; return 0; }
