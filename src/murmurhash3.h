#pragma once
#ifndef MURMURHASH3_H
#define MURMURHASH3_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	uint32_t MurmurHash3_32(const void*, size_t, uint32_t);

#ifdef __cplusplus
}
#endif

#endif // MURMURHASH3_H