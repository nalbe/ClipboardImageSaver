#pragma once




// Retrieve the path to the INI file
BOOL GetIniFilePath(
	LPTSTR lpBuffer,      // Pointer to the buffer that receives the INI file path
	DWORD nBufferLength   // Size of the buffer in characters
);

// Reads an integer value from the specified section and key in the INI file
INT ReadIniInt(
	LPCTSTR section,      // Section name in the INI file to read from
	LPCTSTR key,          // Key name within the section to read
	INT defaultValue,     // Value to return if the key isn’t found
	LPCTSTR configFilePath       // Path to the INI file
);

// Reads a string from the specified section and key in the INI file
void ReadIniString(
	LPCTSTR section,      // Section name in the INI file to read from
	LPCTSTR key,          // Key name within the section to read
	LPCTSTR defaultValue, // String to return if the key isn’t found
	LPTSTR buffer,        // Buffer to store the retrieved string
	DWORD bufferSize,     // Size of the buffer in characters
	LPCTSTR configFilePath       // Path to the INI file
);

// Writes an integer value to the specified section and key in the INI file
void WriteIniInt(
	LPCTSTR section,      // Section name in the INI file to write to
	LPCTSTR key,          // Key name within the section to write
	INT value,            // Integer value to write
	LPCTSTR configFilePath       // Path to the INI file
);

// Writes a string value to the specified section and key in the INI file
void WriteIniString(
	LPCWSTR section,      // Section name in the INI file to write to (wide-character)
	LPCWSTR key,          // Key name within the section to write (wide-character)
	LPCWSTR value,        // String value to write (wide-character)
	LPCWSTR configFilePath       // Path to the INI file (wide-character)
);



