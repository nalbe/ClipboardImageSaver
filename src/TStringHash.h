#pragma once

// Implementation-specific headers
#include "CustomIncludes\murmurhash3.h"

// Standard library headers
#include <string>

// Windows system headers
#include <tchar.h>



// Windows-compatible string type
using tstring = std::basic_string<TCHAR>;



struct TStringHash
{
	size_t operator()(const tstring& s) const
	{
		MurmurHash3_32 hasher(0); // Seed = 0

		// Calculate byte length considering character size
		const size_t byteSize = s.size() * sizeof(TCHAR);

		return hasher(
			reinterpret_cast<const uint8_t*>(s.data()),
			byteSize
		);
	}
};




/*
Usage example:

	std::unordered_set<tstring, TCharHash> uset;

*/



