#include "murmurhash3.h"
#include "string.h"


uint32_t MurmurHash3_32(const void* key, size_t len, uint32_t seed)
{
	const uint8_t* data = (const uint8_t*)key;
	size_t nblocks = len / 4;
	uint32_t h1 = seed;

	const uint32_t c1 = 0xcc9e2d51;
	const uint32_t c2 = 0x1b873593;

	// Body: Process 4-byte blocks
	const uint8_t* blocks = data;
	for (size_t i = 0; i < nblocks; i++) {
		uint32_t k1;
		memcpy(&k1, blocks + i * 4, sizeof(k1)); // Safe unaligned read
		k1 *= c1;
		k1 = (k1 << 15) | (k1 >> 17); // Rotate left 15
		k1 *= c2;

		h1 ^= k1;
		h1 = (h1 << 13) | (h1 >> 19); // Rotate left 13
		h1 = h1 * 5 + 0xe6546b64;
	}

	// Tail: Process remaining bytes
	const uint8_t* tail = data + nblocks * 4;
	uint32_t k1 = 0;
	switch (len & 3) {
	case 3: k1 ^= tail[2] << 16;
	case 2: k1 ^= tail[1] << 8;
	case 1: k1 ^= tail[0];
		k1 *= c1;
		k1 = (k1 << 15) | (k1 >> 17);
		k1 *= c2;
		h1 ^= k1;
	}

	// Finalization
	h1 ^= len;
	h1 ^= h1 >> 16;
	h1 *= 0x85ebca6b;
	h1 ^= h1 >> 13;
	h1 *= 0xc2b2ae35;
	h1 ^= h1 >> 16;

	return h1;
}

