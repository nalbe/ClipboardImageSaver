#include <Windows.h>
#include <tchar.h>
#include <shlwapi.h>
#include "IniSettings.h"

#pragma comment(lib, "Shlwapi.lib")




// Retrieves the path to an INI configuration file
BOOL GetIniFilePath(LPTSTR lpBuffer, DWORD nBufferLength)
{
	// Retrieve the full path of the executable
	if (GetModuleFileName(NULL, lpBuffer, nBufferLength)) {
		// Extract the file name from the path
		LPTSTR fileName = PathFindFileName(lpBuffer);
		// Remove the extension from the file name in place
		if (PathRenameExtension(fileName, _T(".ini"))) {
			return TRUE;
		}
	}
	return FALSE;
}

// Reads an integer value from the specified section and key in the INI file
INT ReadIniInt(LPCTSTR section, LPCTSTR key, INT defaultValue, LPCTSTR iniPath)
{
	return GetPrivateProfileInt(section, key, defaultValue, iniPath);
}

// Reads a string from the specified section and key in the INI file
void ReadIniString(LPCTSTR section, LPCTSTR key, LPCTSTR defaultValue, LPTSTR buffer, DWORD bufferSize, LPCTSTR iniPath)
{
	GetPrivateProfileString(section, key, defaultValue, buffer, bufferSize, iniPath);
}

// Writes an integer value to the specified section and key in the INI file
void WriteIniInt(LPCTSTR section, LPCTSTR key, INT value, LPCTSTR iniPath)
{
	TCHAR strValue[16];
	_stprintf_s(strValue, _T("%d"), value);
	WritePrivateProfileString(section, key, strValue, iniPath);
}

// Writes a string value to the specified section and key in the INI file
void WriteIniString(LPCWSTR section, LPCWSTR key, LPCWSTR value, LPCWSTR iniPath)
{
	WritePrivateProfileString(section, key, value, iniPath);
}



