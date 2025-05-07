
// Implementation-specific headers
#include "ClipboardImageSaver.h"                         // Main header
#include "resource.h"                                    // Resource identifiers
#include "AppDefine.h"                                   // Application-wide definitions and constants
#include "EditDialog.h"                                  // Edit dialog window
#include "CustomIncludes\murmurhash3.h"                  // MurmurHash3 implementation for hashing
#include "CustomIncludes\WinApi\ThemeManager.h"          // Dark mode support
#include "CustomIncludes\WinApi\MessageBoxNotifier.h"    // MessageBox notification handler
#include "CustomIncludes\WinApi\BalloonNotifier.h"       // BalloonNotification handler
#include "CustomIncludes\WinApi\Debouncer.h"             // Debounce helper
#include "CustomIncludes\WinApi\IniFileManager.h"        // .ini file settings management

// Standard library headers
#include <unordered_set>         // Container

// Windows system headers
#include <windows.h>             // Core Windows API definitions (e.g., HWND, WPARAM, SendMessage)
#include <gdiplus.h>             // GDI+ for graphics and image processing
#include <tchar.h>               // TCHAR support for Unicode/ANSI compatibility (e.g., _T macro)

// Library links
#pragma comment(lib, "gdiplus.lib")



// Constants definitions
UINT CF_PNG = []() {
#ifndef PNG
	return RegisterClipboardFormat(_T("PNG"));
#else
	return GetClipboardFormat(PNG);
#endif
	}();



// Application state
namespace Settings
{
	const DWORD WhiteListMaxChars = MAX_PATH;

	TCHAR procWhiteList[WhiteListMaxChars];
	BOOL isNotificationsEnabled{};
	BOOL isWhitelistEnabled{};
	std::unordered_set<RawData, MurmurHash3_32> whitelistHashes{};
	IniFileManager ini{};

	// Application-wide constants for naming and identification
	LPCTSTR MainName            = _T("Clipboard Image Saver");
	LPCTSTR MainClassName       = _T("CISClassname");
	LPCTSTR MutexName           = _T("CISInstance");
}


// Namespace for INI configuration constants
namespace IniConfig
{
	// Sections
	constexpr LPCTSTR NOTIFICATIONS = _T("Notifications");
	constexpr LPCTSTR WHITELIST     = _T("Whitelist");

	// Keys
	namespace Notifications
	{
		constexpr LPCTSTR ENABLED = _T("Enabled");
	}
	namespace Whitelist
	{
		constexpr LPCTSTR ENABLED = _T("Enabled");
		constexpr LPCTSTR LIST    = _T("List");
	}
}


using EMC_ = ErrorMessageConverter;


// Enum declarations
enum class ClipboardResult : unsigned
{
	Success,
	NoData,
	ConversionFailed,
	LockFailed,
	UnchangedContent,
	SaveFailed,
	InvalidParameter
};


// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);



// Opens the specified folder in Windows Explorer.
BOOL OpenFolderInExplorer()
{
	TCHAR szDirectoryPath[MAX_PATH]{};
	if (!GetCurrentDirectory(MAX_PATH, szDirectoryPath)) { return FALSE; }

	// Open the folder in Explorer
	HINSTANCE hResult = ShellExecute(
		NULL,                // No parent window
		_T("open"),             // Operation ("open" or "explore")
		szDirectoryPath,     // Path to the folder
		NULL,                // No parameters
		NULL,                // Default directory
		SW_SHOWNORMAL        // Show window normally
	);

	// Check for errors (return value <= 32 indicates failure)
	return ((intptr_t)hResult > 32);
}

// Converts CR and LF characters to printable placeholders for file storage
BOOL FormatTextForStorage(LPCTSTR cszSrc, LPTSTR szDest, DWORD cchMax)
{
	if (!cszSrc or !szDest) { return FALSE; }

	LPCTSTR pSrcIt = cszSrc;
	LPTSTR pDestIt = szDest;

	for (; *pSrcIt and (pDestIt < szDest + cchMax); ++pSrcIt) {
		if (*pSrcIt == _T('\n')) {
			*pDestIt++ = _T('|');  // Forbidden for filename
		}
		else if (*pSrcIt == _T('\r')) {} // Drop '\r' symbol
		else {
			*pDestIt++ = *pSrcIt;
		}
	}
	*pDestIt = _T('\0');

	return (*pSrcIt == _T('\0'));
}

// Reverses the newline placeholder conversion from file back to original format
BOOL RestoreTextFromStorage(LPCTSTR cszSrc, LPTSTR szDest, DWORD cchMax)
{
	if (!cszSrc or !szDest) { return FALSE; }

	// Handle in-place replacement by processing backward
	DWORD cchOriginalLen{};
	DWORD nPipeCount{};
	LPCTSTR INVALID_CHARS = _T("\\/:*?\"<>");

	// Count '|' characters to determine new length
	for (LPCTSTR it{ cszSrc }; *it != _T('\0'); ++it, ++cchOriginalLen) {
		if (*it == _T('|')) { ++nPipeCount; }
		if (_tcschr(INVALID_CHARS, *it)) { return FALSE; }
	}

	const DWORD cchNewLen = cchOriginalLen + nPipeCount;
	if (cchNewLen > cchMax) { return FALSE; }

	// Start from the end of the original and new buffers
	LPCTSTR pSrcIt = cszSrc + cchOriginalLen - 1;
	LPTSTR pDestIt = szDest + cchNewLen - 1;

	for (szDest[cchNewLen] = _T('\0'); pSrcIt >= cszSrc; --pSrcIt, --pDestIt) {
		if (*pSrcIt == _T('|')) {
			*pDestIt-- = _T('\n'); // Write '\n' first (reverse order)
			*pDestIt = _T('\r');
		}
		else {
			*pDestIt = *pSrcIt;
		}
	}

	return TRUE;
}

// Updates the whitelist hash cache
void UpdateWhitelistCache()
{
	LPCTSTR cszDelim = _T("\r\n");
	LPTSTR szContext = NULL;
	LPTSTR szCopy = _tcsdup(Settings::procWhiteList);

	Settings::whitelistHashes.clear();
	LPTSTR szToken = _tcstok_s(szCopy, cszDelim, &szContext);
	while (szToken) {
		Settings::whitelistHashes.insert({
			reinterpret_cast<const PBYTE>(szToken),
			_tcslen(szToken) * sizeof(TCHAR)
		});
		szToken = _tcstok_s(NULL, cszDelim, &szContext);
	}

	free(szCopy);
}

// Updates a specific int setting in a configuration file
void UpdateSetting(LPCTSTR cszSection, LPCTSTR cszKey, INT nData)
{
	if (cszSection == IniConfig::WHITELIST) {
		if (cszKey == IniConfig::Whitelist::ENABLED) {
			Settings::isWhitelistEnabled = (BOOL)nData;
		}
	}
	else if (cszSection == IniConfig::NOTIFICATIONS) {
		if (cszKey == IniConfig::Notifications::ENABLED) {
			Settings::isNotificationsEnabled = (BOOL)nData;
		}
	}

	// Prevent automatic file creation in WriteIni* functions
	if (!Settings::ini.IsFileExists()) { return; }

	Settings::ini.WriteInt(cszSection, cszKey, (INT)nData);
}

// Updates a specific string setting in a configuration file
void UpdateSetting(LPCTSTR cszSection, LPCTSTR cszKey, LPTSTR szText, DWORD cchTextMax)
{
	if (!szText) { return; }

	if (cszSection == IniConfig::WHITELIST) {
		if (cszKey == IniConfig::Whitelist::LIST) {
			_tcscpy_s(Settings::procWhiteList, Settings::WhiteListMaxChars, szText);
			UpdateWhitelistCache();
			FormatTextForStorage(Settings::procWhiteList, szText, cchTextMax);
		}
	}

	// Prevent automatic file creation in WriteIni* functions
	if (!Settings::ini.IsFileExists()) { return; }

	Settings::ini.WriteString(cszSection, cszKey, szText);
}

// Function to compute hash of an LPCTSTR and check existence in the whitelist
BOOL IsStringWhitelisted(LPCTSTR cszText)
{
	if (!cszText) { return FALSE; }

	auto it = Settings::whitelistHashes.find({
		reinterpret_cast<LPCBYTE>(cszText),
		_tcslen(cszText) * sizeof(TCHAR)
	});

	return it != Settings::whitelistHashes.end();  // Check if hash exists in whitelist
}

// Generates a filename string
LPCTSTR GenerateFilename()
{
	static TCHAR szBuffer[MAX_PATH]{};
	static DWORD dwBaseLength{};              // Directory + prefix length
	static LPCTSTR cszPrefix = _T("\\screenshot_");
	static LPCTSTR cszExtension = _T(".png");
	static const BYTE byPrefixLength = 12;    // Length of prefix without null terminator
	static const BYTE byTimestampLength = 18; // Exact length of "%04d%02d%02d_%02d%02d%02d%03d"
	static const BYTE byExtensionLength = 4;  // Length of ".png"

	if (!dwBaseLength) {
		DWORD dwDirectoryLength = GetCurrentDirectory(MAX_PATH, szBuffer);
		if (dwDirectoryLength == 0 or
			dwDirectoryLength + byPrefixLength + byTimestampLength + byExtensionLength >= MAX_PATH)
		{
			szBuffer[0] = _T('\0');
			return NULL;
		}

		// Add prefix
		_tcscpy_s(szBuffer + dwDirectoryLength, MAX_PATH - dwDirectoryLength, cszPrefix);
		// Add prefix length
		dwBaseLength = dwDirectoryLength + byPrefixLength;
	}

	// Generate timestamp
	SYSTEMTIME st;
	GetLocalTime(&st); // Get time with milliseconds

	// Format directly into buffer at baseLength (YYYYMMDD_HHMMSSmmm)
	_stprintf_s(
		szBuffer + dwBaseLength,   // Target position
		byTimestampLength + 1,     // Max allowed chars: 18 + null terminator
		_T("%04d%02d%02d_%02d%02d%02d%03d"),  // Format: YYYYMMDD_HHMMSSmmm
		st.wYear,                  // 4-digit year
		st.wMonth,                 // 2-digit month
		st.wDay,                   // 2-digit day
		st.wHour,                  // 2-digit hour
		st.wMinute,                // 2-digit minute
		st.wSecond,                // 2-digit second
		st.wMilliseconds           // 3-digit milliseconds
	);

	// Add extension
	_tcscpy_s(
		szBuffer + dwBaseLength + byTimestampLength,
		MAX_PATH - dwBaseLength - byTimestampLength,
		cszExtension
	);

	return szBuffer;
}

// Initialize global settings with defaults or values read from the INI file
BOOL InitializeDefaultSettings()
{
	Settings::isNotificationsEnabled =
		Settings::ini.ReadInt(
			IniConfig::NOTIFICATIONS, IniConfig::Notifications::ENABLED,
			TRUE
		);
	Settings::isWhitelistEnabled =
		Settings::ini.ReadInt(
			IniConfig::WHITELIST, IniConfig::Whitelist::ENABLED,
			TRUE
		);

	const DWORD cchBuffer = Settings::WhiteListMaxChars;
	TCHAR szBuffer[cchBuffer];
	Settings::ini.ReadString(
		IniConfig::WHITELIST, IniConfig::Whitelist::LIST,
		_T("svchost.exe"),
		szBuffer, cchBuffer
	);
	RestoreTextFromStorage(szBuffer, Settings::procWhiteList, Settings::WhiteListMaxChars);
	UpdateWhitelistCache();

	return TRUE;
}

// Helper function to get the PNG encoder CLSID
INT GetEncoderClsid(LPCTSTR cszFormat, CLSID* pClsid)
{
	if (!cszFormat or !pClsid) { return -1; }

	UINT numEncoders{};
	UINT dwPathSize{};

	Gdiplus::GetImageEncodersSize(&numEncoders, &dwPathSize);
	if (numEncoders == 0 or dwPathSize == 0) {
		return -1;
	}

	Gdiplus::ImageCodecInfo* pImageCodecInfo = static_cast<Gdiplus::ImageCodecInfo*>(malloc(dwPathSize));
	if (!pImageCodecInfo) {
		return -1;
	}

	Gdiplus::GetImageEncoders(numEncoders, dwPathSize, pImageCodecInfo);

#pragma warning(push)
#pragma warning(disable: 6385)
	for (UINT j{}; j < numEncoders; ++j) {
		if (_tcscmp(pImageCodecInfo[j].MimeType, cszFormat) == 0) {
			*pClsid = pImageCodecInfo[j].Clsid;
			free(pImageCodecInfo);
			return j;
		}
	}
#pragma warning(pop)

	free(pImageCodecInfo);
	return -1;
}

// Function to save PNG to file
BOOL SavePNGToFile(HGLOBAL hData, LPCTSTR cszFilename)
{
	if (!hData or !cszFilename) { return FALSE; }

	// Handle PNG data directly
	LPVOID pngData = GlobalLock(hData);
	BOOL bSuccess{};

	if (pngData) {
		SIZE_T cbDataSize = GlobalSize(hData);
		HANDLE hFile = CreateFile(cszFilename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile != INVALID_HANDLE_VALUE) {
			DWORD bytesWritten;
			WriteFile(hFile, pngData, (DWORD)cbDataSize, &bytesWritten, NULL);
			CloseHandle(hFile);
			bSuccess = (bytesWritten == cbDataSize);
		}
		GlobalUnlock(hData);
	}
	return bSuccess;
}

// Function to save DIB to PNG file
BOOL SaveDIBToFile(HGLOBAL hData, LPCTSTR cszFilename)
{
	if (!hData or !cszFilename) { return FALSE; }

	// Lock the DIB to access its data
	BITMAPINFO* pbmi = (BITMAPINFO*)GlobalLock(hData);
	if (!pbmi) { return FALSE; }

	// Calculate the offset to the pixel data
	DWORD dwColorTableSize{};
	if (pbmi->bmiHeader.biBitCount <= 8) {
		dwColorTableSize = (pbmi->bmiHeader.biClrUsed ? pbmi->bmiHeader.biClrUsed : (1 << pbmi->bmiHeader.biBitCount)) * sizeof(RGBQUAD);
	}
	LPVOID pPixels = (BYTE*)pbmi + pbmi->bmiHeader.biSize + dwColorTableSize;

	// Create a GDI+ Bitmap from the DIB
	Gdiplus::Bitmap bitmap(pbmi, pPixels);

	// Get the PNG encoder CLSID
	CLSID pngClsid;
	if (GetEncoderClsid(_T("image/png"), &pngClsid) < 0) {
		GlobalUnlock(hData);
		return FALSE;
	}

	// Save the bitmap as PNG
	Gdiplus::Status gdiStatus = bitmap.Save(cszFilename, &pngClsid, NULL);

	// Clean up
	GlobalUnlock(hData);
	return gdiStatus == Gdiplus::Ok;
}

// Retrieves the executable path of the clipboard owner process
LPCTSTR RetrieveClipboardOwner()
{
	static TCHAR szExePath[MAX_PATH]; // Buffer for the executable path

	HWND hClipboardOwner = GetClipboardOwner();
	if (!hClipboardOwner) { return NULL; }

	DWORD dwProcessId{};
	GetWindowThreadProcessId(hClipboardOwner, &dwProcessId);
	if (!dwProcessId) { return NULL; }

	HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwProcessId);
	if (!hProcess) { return NULL; }

	DWORD dwPathSize = MAX_PATH;
	BOOL bSuccess = QueryFullProcessImageName(hProcess, 0, szExePath, &dwPathSize);
	CloseHandle(hProcess);
	if (!bSuccess) { return NULL; }

	// Extract the executable name
	LPCTSTR cszExeName = _tcsrchr(szExePath, _T('\\'));
	if (!cszExeName) { return NULL; }
	++cszExeName; // Move past the backslash

	return cszExeName;
}

// Convert bitmap to DIB
BOOL BitmapToDIB(HGLOBAL hClipboardData)
{
	if (!hClipboardData) { return FALSE; }

	HBITMAP hBitmap = (HBITMAP)hClipboardData;
	if (hBitmap) {
		hClipboardData = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + GetBitmapBits(hBitmap, 0, NULL));
		if (hClipboardData) {
			BITMAPINFOHEADER* pbi = (BITMAPINFOHEADER*)GlobalLock(hClipboardData);
			if (pbi) { // Check if lock succeeded
				GetDIBits(GetDC(NULL), hBitmap, 0, 0, NULL, (BITMAPINFO*)pbi, DIB_RGB_COLORS);
				GlobalUnlock(hClipboardData);
				return TRUE;
			}
		}
	}
	return FALSE;
}

// Retrieves image data and its format from the clipboard
HGLOBAL GetClipboardImageData(INT* pFormat)
{
	if (!pFormat) { return 0; }

	static UINT uFormatPriorityList[]{ CF_PNG, CF_DIBV5, CF_DIB, CF_BITMAP };

	HGLOBAL hClipboardData{};
	HGLOBAL hCopy{};
	*pFormat = 0;

	INT nFormat = GetPriorityClipboardFormat(uFormatPriorityList, _countof(uFormatPriorityList));
	if (nFormat) {
		hClipboardData = GetClipboardData(nFormat);
		if (hClipboardData) {
			SIZE_T cbDataSize = GlobalSize(hClipboardData);
			hCopy = GlobalAlloc(GMEM_MOVEABLE, cbDataSize);
			if (hCopy) {
				LPVOID pSrc = GlobalLock(hClipboardData);
				LPVOID pDest = GlobalLock(hCopy);
				if (pSrc && pDest) {
					memcpy(pDest, pSrc, cbDataSize);
					*pFormat = nFormat;
				}
				GlobalUnlock(hClipboardData);
				GlobalUnlock(hCopy);
			}
		}
	}
	return hCopy; // Caller MUST free this handle
}

// Processes clipboard data and handles related operations
ClipboardResult HandleClipboardData(LPTSTR szFormat, UINT cchFormat)
{
	static DWORD dwLastDataHash{};
	static SIZE_T cbLastDataSize{};

	if (!szFormat) { return ClipboardResult::InvalidParameter; }

	INT nFormat{};
	HGLOBAL hClipboardData = GetClipboardImageData(&nFormat);

	_tcscpy_s(szFormat, cchFormat, [&]() {
		if (nFormat == CF_PNG)    return _T("PNG");
		if (nFormat == CF_DIBV5)  return _T("DIBV5");
		if (nFormat == CF_DIB)    return _T("DIB");
		if (nFormat == CF_BITMAP) return _T("BITMAP");
		else                      return _T("unknown");
		}()
	);

	if (!hClipboardData or nFormat <= 0) {
		return ClipboardResult::NoData;
	}

	// Convert CF_BITMAP to CF_DIB if needed
	if (nFormat == CF_BITMAP) {
		if (!BitmapToDIB(hClipboardData)) {
			GlobalFree(hClipboardData);
			return ClipboardResult::ConversionFailed;
		}
		nFormat = CF_DIB;
	}

	LPBYTE lpcbData = static_cast<LPBYTE>(GlobalLock(hClipboardData));
	if (!lpcbData) {
		GlobalFree(hClipboardData);
		return ClipboardResult::LockFailed;
	}

	// Calculate content hash
	const SIZE_T cbDataSize = GlobalSize(hClipboardData);
	const DWORD dwDataHash = (DWORD)MurmurHash3_32{}.computeHash(lpcbData, cbDataSize);
	GlobalUnlock(hClipboardData);

	// Check for duplicate content
	if (cbDataSize == cbLastDataSize and dwDataHash == dwLastDataHash) {
		GlobalFree(hClipboardData);
		return ClipboardResult::UnchangedContent;
	}

	// Generate filename and save
	LPCTSTR cszFilename = GenerateFilename();
	if (!cszFilename) {
		GlobalFree(hClipboardData);
		return ClipboardResult::SaveFailed;
	}

	BOOL bResult{};
	if (nFormat == CF_PNG) {
		bResult = SavePNGToFile(hClipboardData, cszFilename);
	}
	else {
		bResult = SaveDIBToFile(hClipboardData, cszFilename);
	}

	GlobalFree(hClipboardData);

	if (!bResult) {
		return ClipboardResult::SaveFailed;
	}

	// Update state only after successful save
	dwLastDataHash = dwDataHash;
	cbLastDataSize = cbDataSize;

	return ClipboardResult::Success;
}

// Tray Icon initialization
BOOL InitializeNotifyIcon(NOTIFYICONDATA* pNotifyIconData, HWND hWnd, HICON* pIcon)
{
	if (!pNotifyIconData or !hWnd or !pIcon) { return FALSE; }

	// Load the icon
	*pIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_MAIN_ICON));
	if (!*pIcon) { return FALSE; }

	// Initialize NOTIFYICONDATA with modern settings
	pNotifyIconData->uFlags =
		NIF_ICON |		// The tray icon
		NIF_MESSAGE |   // Event handling
		NIF_TIP |       // Tooltip text
		NIF_SHOWTIP;    // Ensures the tooltip appears when hovering (Windows 10/11 enhancement)

	_tcscpy_s(pNotifyIconData->szTip, Settings::MainName);  // Sets the tooltip text (paired with NIF_TIP)
	pNotifyIconData->cbSize = sizeof(NOTIFYICONDATA);        // Specifies the size of the structure, required by Shell_NotifyIcon
	pNotifyIconData->hWnd = hWnd;                            // Associates the tray icon with a window to receive callback messages
	pNotifyIconData->uID = IDI_NOTIFY_ICON;                  // A unique identifier for the tray icon
	pNotifyIconData->uCallbackMessage = WM_APP_TRAYICON;     // Defines the custom message
	pNotifyIconData->hIcon = *pIcon;                         // Specifies the icon to display in the tray (paired with NIF_ICON)
	// Required for Windows 10/11
	pNotifyIconData->dwState = NIS_SHAREDICON;               // Indicates the icon is shared and shouldn’t be deleted when the notification is removed
	pNotifyIconData->dwStateMask = NIS_SHAREDICON;           // Specifies which state bits to modify

	// First add the icon to the tray
	if (!Shell_NotifyIcon(NIM_ADD, pNotifyIconData)) { return FALSE; }  // Adds the tray icon to the system tray

	// Set version AFTER adding
	pNotifyIconData->uVersion = NOTIFYICON_VERSION_4;      // Sets the tray icon version to enable modern features (e.g., balloon tips, improved behavior on Windows 10/11)

	return Shell_NotifyIcon(NIM_SETVERSION, pNotifyIconData);
}

// Initializes the GDI+ library for image processing
Gdiplus::Status InitializeGDIPlus(ULONG_PTR* pGdiPlusToken)
{
	if (!pGdiPlusToken) { return Gdiplus::Status::InvalidParameter; }

	Gdiplus::GdiplusStartupInput startupInput;
	return Gdiplus::GdiplusStartup(pGdiPlusToken, &startupInput, NULL);
}

// Creates a popup menu for the system tray
BOOL CreateTrayContextMenu(HMENU* pMenu)
{
	if (!pMenu) { return FALSE; }

	*pMenu = CreatePopupMenu();
	if (!*pMenu) { return FALSE; }

	AppendMenu(*pMenu, MF_STRING, IDM_TRAY_OPEN_FOLDER,
		_T("Open folder")
	);
	AppendMenu(*pMenu, MF_SEPARATOR, IDM_TRAY_SEPARATOR, NULL);
	AppendMenu(*pMenu,
		MF_STRING | (Settings::isWhitelistEnabled ? MF_CHECKED : MF_UNCHECKED),
		IDM_TRAY_TOGGLE_WHITELIST, _T("Use whitelist")
	);
	AppendMenu(*pMenu,
		MF_STRING | (Settings::isWhitelistEnabled ? 0 : MF_GRAYED),
		IDM_TRAY_EDIT_WHITELIST, _T("Edit Whitelist")
	);
	AppendMenu(*pMenu,
		MF_STRING | (Settings::isNotificationsEnabled ? MF_CHECKED : MF_UNCHECKED),
		IDM_TRAY_TOGGLE_NOTIFICATIONS, _T("Show notifications")
	);
	AppendMenu(*pMenu, MF_SEPARATOR, IDM_TRAY_SEPARATOR, NULL);
	AppendMenu(*pMenu, MF_STRING, IDM_TRAY_EXIT,
		_T("Exit")
	);

	return *pMenu != NULL;
}

// Shows a popup menu for the system tray
BOOL ShowTrayContextMenu(HWND hWnd)
{
	HMENU hMenu;

	if (CreateTrayContextMenu(&hMenu)) {
		SetForegroundWindow(hWnd); // Required for focus

		POINT pt;
		GetCursorPos(&pt);

		TrackPopupMenu(
			hMenu,
			TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_NONOTIFY,
			pt.x, pt.y,
			0, hWnd, NULL
		);

		DestroyMenu(hMenu);
		return TRUE;
	}
	return FALSE;
}

// Attempts to open the clipboard with retry logic on access denial
BOOL TryOpenClipboard()
{
	BOOL bResult = OpenClipboard(NULL);
	if (bResult) { return TRUE; }

	DWORD dwError = GetLastError();
	if (dwError == ERROR_ACCESS_DENIED) { // Clipboard is being held by another process
		INT nRetries = 4;
		UINT uDelay = 50;  // ms

		// Retry logic with progressive backoff
		do {
			Sleep(uDelay);
			uDelay *= 2;  // Exponential backoff
		} while (nRetries-- > 0 and !(bResult = OpenClipboard(NULL)));
	}
	return bResult;
}

// Validates a string for Windows application names by checking for invalid characters
BOOL CheckTextCorrectness(LPCTSTR lpcszText)
{
	if (!lpcszText) { return FALSE; }

	// These are typically invalid filename characters in Windows
	for (LPCTSTR INVALID_CHARS{ _T("\\/:*?\"<>|") }; *lpcszText; ++lpcszText) {
		if (_tcschr(INVALID_CHARS, *lpcszText)) {
			return FALSE;
		}
	}
	return TRUE;
}

// Create Main window
HWND CreateDummyWindow(HINSTANCE hInstance)
{
	return CreateWindow(
		Settings::MainClassName,
		Settings::MainName,
		WS_POPUP,  // Invisible but valid parent
		0, 0, 0, 0,
		(HWND)NULL, (HMENU)NULL, hInstance, (LPVOID)NULL
	);
}

// Register Window Class
ATOM RegisterWindowClass(HINSTANCE hInstance, WNDCLASSEX* wcex)
{
	wcex->cbSize = sizeof(WNDCLASSEX);
	wcex->lpfnWndProc = WndProc;                      // Window procedure
	wcex->hInstance = hInstance;                      // App instance
	wcex->lpszClassName = Settings::MainClassName;  // Class name
	wcex->hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN_ICON));

	return RegisterClassEx(wcex);
}


// Window procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	// Variable to track exit code
	static INT nExitCode{}; // Default: success

	static HICON hIcon{};
	static ULONG_PTR pGdiPlusToken{};
	static NOTIFYICONDATA notifyIconData{};


	switch (uMsg)
	{

	case WM_CLIPBOARDUPDATE:
	{
		static Debouncer debouncer{ 50 };
		static const UINT uMaxFormatStringLength = 64;
		static TCHAR szClipboardFormatBuffer[uMaxFormatStringLength];

		// Debounce
		if (!debouncer.ShouldProcess()) { break; }

		if (!TryOpenClipboard()) {
			BalloonNotifier{
				{ _T("System Error") },
				{ _T("Failed to access clipboard." EOL_ "%s"), EMC_(GetLastError()) }
			}.ShowError(&notifyIconData);
			break;
		}

		// Retrieves the name of the clipboard data owner
		LPCTSTR cszClipboardOwner = RetrieveClipboardOwner();
		if (!cszClipboardOwner) {
			CloseClipboard();
			break;
		}

		// Checks if the 'Restrict to System' option is enabled
		if (Settings::isWhitelistEnabled == TRUE) {
			if (!IsStringWhitelisted(cszClipboardOwner)) {
				CloseClipboard();
				break;
			}
		}

		// Helper function for error cases
		const auto HandleClipboardError = [&](LPCTSTR szTitle, LPCTSTR szMessage) {
			if (Settings::isNotificationsEnabled) {
				BalloonNotifier{
					{ szTitle },
					{ _T("%s." EOL_ "%s"), EMC_(GetLastError()) }
				}.ShowError(&notifyIconData);
			}
			CloseClipboard();
			nExitCode = -1;
			DestroyWindow(hWnd);
		};

		// Helper function for success notifications
		const auto ShowStatusNotification = [&](LPCTSTR szTitle) {
			if (Settings::isNotificationsEnabled) {
				BalloonNotifier{
					{ szTitle },
					{ _T("Owner:  %s" EOL_ "Type:  %s"), cszClipboardOwner, szClipboardFormatBuffer }
				}.ShowInfo(&notifyIconData);
			}
		};

		ClipboardResult clipboardResult = 
			HandleClipboardData(szClipboardFormatBuffer, uMaxFormatStringLength);

		switch (clipboardResult) {
		case ClipboardResult::Success:
			ShowStatusNotification(_T("Clipboard Data Captured"));
			break;
		case ClipboardResult::UnchangedContent:
			//ShowStatusNotification(_T("Clipboard Data Ignored"));
			break;
		case ClipboardResult::ConversionFailed:
			HandleClipboardError(_T("Image Conversion Error"), _T("Failed to convert bitmap to DIB format"));
			return -1;
		case ClipboardResult::LockFailed:
			HandleClipboardError(_T("Memory Error"), _T("Failed to lock clipboard memory"));
			return -1;
		case ClipboardResult::SaveFailed:
			HandleClipboardError(_T("Save Error"), _T("Failed to save image to file"));
			return -1;
		default: break; }

		CloseClipboard();
		break;
	}

	case WM_COMMAND:
	{
		WORD wNotificationCode = HIWORD(wParam);
		WORD wCommandId = LOWORD(wParam);

		if (wNotificationCode == 0) {  // Menu/accelerator
			if (wCommandId == IDM_TRAY_EXIT) {
				SendMessage(hWnd, WM_CLOSE, 0, 0);
				break;
			}

			if (wCommandId == IDM_TRAY_OPEN_FOLDER) {
				if (!OpenFolderInExplorer()) {
					BalloonNotifier{
						{ _T("System Error") },
						{ _T("Failed to open folder in File Explorer." EOL_ "%s"), EMC_(GetLastError()) }
					}.ShowWarning(&notifyIconData);
				}
				break;
			}

			if (wCommandId == IDM_TRAY_TOGGLE_WHITELIST) {
				UpdateSetting(IniConfig::WHITELIST, IniConfig::Whitelist::ENABLED,
					(INT)!Settings::isWhitelistEnabled);
				break;
			}

			if (wCommandId == IDM_TRAY_EDIT_WHITELIST) {
				// Initialize dialog parameters with initial dialog text and its buffer size
				DialogParams params{  // Copy will be accessible via GWLP_USERDATA
					Settings::procWhiteList, Settings::WhiteListMaxChars
				};

				// Create and show the custom dialog, passing the parameters
				HWND hDialog = ShowCustomDialog(hWnd, &params);
				if (!hDialog) {
					BalloonNotifier{
						{ _T("System Error") },
						{ _T("Failed to create dialog window." EOL_ "%s"), EMC_(GetLastError()) }
					}.ShowError(&notifyIconData);
					break;
				}

				break;
			}

			if (wCommandId == IDM_TRAY_TOGGLE_NOTIFICATIONS) {
				UpdateSetting(IniConfig::NOTIFICATIONS, IniConfig::Notifications::ENABLED,
					(INT)!Settings::isNotificationsEnabled
				);
				break;
			}
		}

		else if (wNotificationCode == 1) {}  // Accelerator (rarely used explicitly)
		else {}  // Control notification
		break;
	}

	case WM_CREATE:
	{
		Gdiplus::Status gdiStatus = InitializeGDIPlus(&pGdiPlusToken);
		if (gdiStatus != Gdiplus::Status::Ok) {
			MessageBoxNotifier{
				{ _T("GDI+ Error") },
				{ _T("Failed to initialize GDI+ library." EOL_ "%s"), gdiStatus }
			}.ShowError(hWnd);
			return -1;
		}

		if (!InitializeNotifyIcon(&notifyIconData, hWnd, &hIcon)) {
			MessageBoxNotifier{
				{ _T("System Error") },
				{ _T("Failed to initialize system tray icon." EOL_ "%s"), EMC_(GetLastError()) }
			}.ShowError(hWnd);
			return -1;
		}

		if (!AddClipboardFormatListener(hWnd)) {
			BalloonNotifier{
				{ _T("System Error") },
				{ _T("Failed to register clipboard listener." EOL_ "%s"), EMC_(GetLastError()) }
			}.ShowError(&notifyIconData);
			return -1;
		}

		if (!CF_PNG) {
			BalloonNotifier{
				{ _T("Clipboard Error") },
				{ _T("PNG format not available.") }
			}.ShowWarning(&notifyIconData);
		}

		if (!ThemeManager::FollowSystemTheme(hWnd)) {
			BalloonNotifier{
				{ _T("System Error") },
				{ _T("Failed to apply system theme." EOL_ "%s"), EMC_(GetLastError()) }
			}.ShowWarning(&notifyIconData);
		}

		break;
	}

	case WM_DESTROY:
	{
		if (!RemoveClipboardFormatListener(hWnd)) {}

		// Remove system tray icon
		Shell_NotifyIcon(NIM_DELETE, &notifyIconData);

		// Release tray icon resources
		if (hIcon) {
			if (!DestroyIcon(hIcon)) {}
		}

		// Shutdown GDI+ if initialized
		if (pGdiPlusToken) {
			Gdiplus::GdiplusShutdown(pGdiPlusToken);
		}

		// Unregister dialog class
		HINSTANCE hInstance = GetModuleHandle(NULL);
		if (hInstance) {
			LPCTSTR className = DIALOG_CLASSNAME;
			WNDCLASS wc{};
			if (GetClassInfo(hInstance, className, &wc)) {
				UnregisterClass(className, hInstance);
			}
		}

		PostQuitMessage(nExitCode);
		break;
	}

	case WM_SETTINGCHANGE:
	{
		if (lParam and CompareStringOrdinal(reinterpret_cast<LPCWCH>(lParam),
			-1, _T("ImmersiveColorSet"), -1, TRUE) == CSTR_EQUAL)
		{
			// Re-apply the correct theme based on current system setting
			HWND hwndFound = FindWindow(DIALOG_CLASSNAME, NULL);
			if (hwndFound) { 
				PostMessage(hwndFound, WM_APP_CUSTOM_MESSAGE,
					MAKEWPARAM(ID_THEME_CHANGED, 0),
					(LPARAM)NULL
				);
			}
		}

		break;
	}

	case WM_APP_TRAYICON:
	{
		switch (LOWORD(lParam))
		{
		case WM_LBUTTONUP:
		{
			//
			break;
		}
		case WM_RBUTTONUP:
		{
			if (!ShowTrayContextMenu(hWnd)) {
				BalloonNotifier{
					{ _T("System Error") },
					{ _T("Failed to create system tray context menu." EOL_ "%s"), EMC_(GetLastError()) }
				}.ShowError(&notifyIconData);
				return 1;
			}
			break;
		}

		default: break;
		}

		break;
	}

	case WM_APP_CUSTOM_MESSAGE:
	{
		WORD wCommandId = LOWORD(wParam);

		if (wCommandId == ID_MULTIPLE_INSTANCES) {
			BalloonNotifier{
				{ _T("Multiple Instances Warning") },
				{ _T("Another instance of this application is already running.") }
			}.ShowWarning(&notifyIconData);
			return 0;
		}
		if (wCommandId == ID_DIALOG_RESULT) {
			if (!HIWORD(wParam)) {  // If the dialog was canceled
				return ERROR_SUCCESS;
			}

			// Retrieve dialog parameters stored in GWLP_USERDATA
			PDialogParams pParams = reinterpret_cast<PDialogParams>(
				GetWindowLongPtr((HWND)lParam, GWLP_USERDATA));
			if (!pParams) { return 1; }  // Return 1 to indicate abortion
			
			// Validate the text in the buffer
			if (!CheckTextCorrectness(pParams->szBuffer)) { return 1; }  // Abort if invalid

			// Update the whitelist setting with the new string
			UpdateSetting(IniConfig::WHITELIST, IniConfig::Whitelist::LIST,
				pParams->szBuffer, pParams->cchMax);

			// Indicate successful processing
			return ERROR_SUCCESS;
		}

		return 1;
	}

	default: break;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}


// Main Application Entry Point
int WINAPI WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR lpCmdLine,
	_In_ int nCmdShow)
{
	// Create a mutex with no security attributes
	HANDLE hMutex = CreateMutex(NULL, TRUE, Settings::MutexName);
	if (!hMutex) {
		MessageBoxNotifier{
			{ _T("Mutex Error") },
			{ _T("Unable to acquire lock." EOL_ "%s"), EMC_(GetLastError()) }
		}.ShowError(NULL);

		MessageBoxNotifier{
			{ _T("Mutex Error") },
			{ _T("Unable to acquire lock."), EMC_(GetLastError()) }
		}.ShowError(NULL);

		return 1;
	}

	DWORD dwMutexResult = GetLastError();
	// If the mutex already exists, another instance is running
	if (dwMutexResult == ERROR_ALREADY_EXISTS) {
		HWND hWnd = FindWindow(Settings::MainClassName, NULL);
		if (hWnd) {
			PostMessage(hWnd, WM_APP_CUSTOM_MESSAGE,
				MAKEWPARAM(ID_MULTIPLE_INSTANCES, 0), (LPARAM)NULL);
		}
		return 0;
	}

	// Read settings if the file exists, or set defaults
	if (!InitializeDefaultSettings()) {
		MessageBoxNotifier{
			{ _T("Settings Error") },
			{ _T("Failed to initialize settings." EOL_ "%s"), EMC_(GetLastError()) }
		}.ShowError(NULL);
		return 1;
	}

	// Enable dark mode support
	if (!ThemeManager::EnableThemeSupport()) {
		MessageBoxNotifier{
			{ _T("System Error") },
			{ _T("Failed to enable theme support." EOL_ "%s"), EMC_(GetLastError()) }
		}.ShowError(NULL);
	}

	// Register class
	WNDCLASSEXW g_wcex{};
	if (!RegisterWindowClass(hInstance, &g_wcex)) {
		MessageBoxNotifier{
			{ _T("System Error") },
			{ _T("Unable to register window class." EOL_ "%s"), EMC_(GetLastError()) }
		}.ShowError(NULL);
		return 1;
	}

	// Create window
	HWND hMainWnd = CreateDummyWindow(hInstance);
	if (!hMainWnd) {
		MessageBoxNotifier{
			{ _T("System Error") },
			{ _T("Unable to create main window." EOL_ "%s"), EMC_(GetLastError()) }
		}.ShowError(NULL);
		if (hMutex) {
			ReleaseMutex(hMutex);
			CloseHandle(hMutex);
		}
		return 1;
	}

	MSG uMsg;
	// Message loop
	while (GetMessage(&uMsg, NULL, 0, 0))
	{
		TranslateMessage(&uMsg);
		DispatchMessage(&uMsg);
	}

	// Cleanup
	UnregisterClass(Settings::MainClassName, GetModuleHandle(NULL));
	if (hMutex) {
		ReleaseMutex(hMutex);
		CloseHandle(hMutex);
	}
	ThemeManager::DisableThemeSupport();

	return 0;
}



