//#define _CRT_SECURE_NO_WARNINGS
#include "ClipboardImageSaver.h"


#ifndef PNG
// Register clipboard format for PNG if not predefined
static UINT CF_PNG = RegisterClipboardFormat(_T("PNG"));
#else
// Use the existing predefined PNG format
static UINT CF_PNG = GetClipboardFormat(PNG); // Assumes `PNG` is a registered format name
#endif


// Application-wide constants for naming and identification
LPCTSTR g_mainName       = _T("Clipboard Image Saver");
LPCTSTR g_mainClassName  = _T("CISClassname");
LPCTSTR MUTEX_NAME       = _T("CISInstance");

// Global handles and structures for window and instance management
HWND g_hMainWnd{};
HANDLE g_hMutex{};
WNDCLASSEXW g_wcex{};

// Defines a structure to hold application settings globally
AppSettings g_settings;


// Namespace for INI configuration constants
namespace IniConfig
{
	// Sections
	constexpr LPCTSTR NOTIFICATIONS  = _T("Notifications");
	constexpr LPCTSTR GENERAL        = _T("General");

	// Keys
	namespace Notifications
	{
		constexpr LPCTSTR ENABLED = _T("Enabled");
	}
	namespace General
	{
		constexpr LPCTSTR RESTRICT_TO_SYSTEM = _T("RestrictToSystem");
	}
}




// Converts a DWORD error code into a human-readable error message string
LPCTSTR ErrorToText(DWORD dwErrorCode)
{
	static LPTSTR pErrorText;
	pErrorText = NULL;

	// Retrieve the system error message for `dwErrorCode`
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |           // Allocate buffer automatically
		FORMAT_MESSAGE_FROM_SYSTEM |               // Use system error message table
		FORMAT_MESSAGE_IGNORE_INSERTS,             // Ignore any insert sequences in the message
		NULL,                                      // No specific message source (using system)
		dwErrorCode,                               // The error code to convert
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language (neutral, system default)
		reinterpret_cast<LPTSTR>(&pErrorText),     // Output buffer for the error message
		0,                                         // Minimum buffer size (ignored due to ALLOCATE_BUFFER)
		NULL                                       // No additional arguments for message inserts
	);

	return pErrorText;
}

// Displays a message box with formatted title and message for error reporting
template <typename... Args>
INT ShowMessageBoxNotification(HWND hWnd, UINT uFlags, LPCTSTR titleFormat, LPCTSTR messageFormat, Args&&... args)
{
	TCHAR title[64]{};    // Buffer for the formatted title (max 64 characters)
	TCHAR message[256]{}; // Buffer for the formatted message (max 256 characters)

	// Format the title string
	_stprintf_s(
		title, _countof(title),
		titleFormat ? titleFormat : _T(""),
		std::forward<Args>(args)...
	);

	// Format the message body
	_stprintf_s(
		message, _countof(message),
		messageFormat ? messageFormat : _T(""),
		std::forward<Args>(args)...
	);

	// Display the message box and return the user’s response (e.g., IDOK, IDCANCEL)
	return MessageBox(hWnd, message, title, uFlags);
}

// Displays a balloon notification with formatted title and message for error reporting
template <typename... Args>
BOOL ShowBalloonNotification(NOTIFYICONDATA* pNotifyIconData, DWORD dwInfoFlags, LPCTSTR titleFormat, LPCTSTR messageFormat, Args&&... args)
{
	if (!pNotifyIconData) { return FALSE; }

	// Set notification icon flags (e.g., NIIF_INFO, NIIF_WARNING)
	pNotifyIconData->dwInfoFlags = dwInfoFlags;

	// Format and set the notification title
	_stprintf_s(
		pNotifyIconData->szInfoTitle,
		_countof(pNotifyIconData->szInfoTitle), // Use szInfoTitle for consistency
		titleFormat ? titleFormat : _T(""),
		std::forward<Args>(args)...
	);

	// Format and set the notification message body
	_stprintf_s(
		pNotifyIconData->szInfo,
		_countof(pNotifyIconData->szInfo),
		messageFormat ? messageFormat : _T(""),
		std::forward<Args>(args)...
	);

	// Update the system tray notification
	return Shell_NotifyIcon(NIM_MODIFY, pNotifyIconData);
}

// Enables dark mode support for the application
BOOL EnableDarkModeSupport()
{
	HMODULE hUxtheme = LoadLibraryEx(_T("uxtheme.dll"), NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!hUxtheme) { return FALSE; }

	// Manually define the function prototype and enum (since they are not in the SDK)
	typedef enum _APP_MODE { Default, AllowDark, ForceDark, ForceLight, Max } APP_MODE;
	// Define the function type for SetPreferredAppMode (Ordinal 135 for Windows 10/11)
	typedef APP_MODE(WINAPI* PFN_SetPreferredAppMode)(APP_MODE appMode);

	// Retrieves the function pointer from the DLL using its ordinal value
	auto _SetPreferredAppMode = reinterpret_cast<PFN_SetPreferredAppMode>(
		GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135))
		);

	if (_SetPreferredAppMode) {
		_SetPreferredAppMode(AllowDark);
	}

	FreeLibrary(hUxtheme);
	return TRUE;
}

// Determines if the system is currently using a dark theme
BOOL IsSystemDarkThemeEnabled()
{
	HMODULE hUxtheme = LoadLibraryEx(_T("uxtheme.dll"), NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!hUxtheme) { return FALSE; }

	// Defines the function signature for ShouldAppsUseDarkMode (ordinal 132)
	typedef BOOL(WINAPI* PFN_ShouldAppsUseDarkMode)();

	// Retrieves the function pointer from the DLL using its ordinal value
	auto _ShouldAppsUseDarkMode = reinterpret_cast<PFN_ShouldAppsUseDarkMode>(
		GetProcAddress(hUxtheme, MAKEINTRESOURCEA(132)));

	// Calls the function if it exists, otherwise defaults to FALSE (light mode)
	BOOL isDark = _ShouldAppsUseDarkMode ? _ShouldAppsUseDarkMode() : FALSE;

	FreeLibrary(hUxtheme);
	return isDark;
}

// Applies the system theme (light or dark) to the specified window
HRESULT FollowSystemTheme(HWND hWnd)
{
	BOOL isDark = IsSystemDarkThemeEnabled();

	return SetWindowTheme(
		hWnd,
		isDark ? _T("DarkMode_Explorer") : _T("Explorer"),
		NULL
	);
}

// Opens the specified folder in Windows Explorer.
BOOL OpenFolderInExplorer(LPCTSTR folderPath)
{
	if (!folderPath) { return FALSE; }

	// Open the folder in Explorer
	HINSTANCE result = ShellExecute(
		NULL,                // No parent window
		L"open",             // Operation ("open" or "explore")
		folderPath,          // Path to the folder
		NULL,                // No parameters
		NULL,                // Default directory
		SW_SHOWNORMAL        // Show window normally
	);

	// Check for errors (return value <= 32 indicates failure)
	return ((intptr_t)result > 32);
}

// Initialize global settings with defaults or values read from the INI file
void InitializeDefaultSettings()
{
	g_settings.showNotifications =
		ReadIniInt(
			IniConfig::NOTIFICATIONS, IniConfig::Notifications::ENABLED,
			TRUE, g_settings.iniPath
		);
	g_settings.restrictToSystem =
		ReadIniInt(
			IniConfig::GENERAL, IniConfig::General::RESTRICT_TO_SYSTEM,
			TRUE, g_settings.iniPath
		);
}

// Fast 32-bit hash function (for binary data)
UINT32 ComputeDataHash(LPCBYTE pData, SIZE_T dataSize)
{
	if (!pData) { return 0; }

	UINT32 seed = 0; // Fixed seed for consistency
	UINT32 hash = MurmurHash3_32(pData, dataSize, seed);
	return hash;
}

// Check if the file exists and is not a directory
BOOL IsFileExists(LPCTSTR filePath) {
	if (!filePath) { return FALSE; }

	DWORD attributes = GetFileAttributes(filePath);
	return (attributes != INVALID_FILE_ATTRIBUTES and
		!(attributes & FILE_ATTRIBUTE_DIRECTORY));
}

// Generates a filename string
LPCTSTR GenerateFilename()
{
	static TCHAR buffer[MAX_PATH]{};
	static DWORD baseLength{}; // Directory + prefix length
	static LPCTSTR prefix = _T("\\screenshot_");
	static LPCTSTR extension = _T(".png");
	static const BYTE prefixLength = 12; // Length of prefix without null terminator
	static const BYTE timestampLength = 18; // Exact length of "%04d%02d%02d_%02d%02d%02d%03d"
	static const BYTE extensionLength = 4; // Length of ".png"

	if (!baseLength) {
		DWORD dirLength = GetCurrentDirectory(MAX_PATH, buffer);
		if (dirLength == 0 || dirLength + prefixLength + timestampLength + extensionLength >= MAX_PATH) {
			// Handle error: directory too long or other failure
			buffer[0] = _T('\0');
			return NULL;
		}
		// Add prefix
		_tcscpy_s(buffer + dirLength, MAX_PATH - dirLength, prefix);
		baseLength = dirLength + prefixLength;
	}

	// Generate timestamp
	SYSTEMTIME st;
	GetLocalTime(&st); // Get time with milliseconds

	// Format directly into buffer at baseLength (YYYYMMDD_HHMMSSmmm)
	_stprintf_s(
		buffer + baseLength,       // Target position
		timestampLength + 1,       // Max allowed chars: 18 + null terminator
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
	_tcscpy_s(buffer + baseLength + timestampLength, MAX_PATH - baseLength - timestampLength, extension);

	return buffer;
}

// Helper function to get the PNG encoder CLSID
INT GetEncoderClsid(LPCTSTR format, CLSID* pClsid)
{
	if (!format or !pClsid) { return -1; }

	UINT num{};
	UINT pathSize{};

	Gdiplus::GetImageEncodersSize(&num, &pathSize);
	if (num == 0 or pathSize == 0) {
		return -1;
	}

	Gdiplus::ImageCodecInfo* pImageCodecInfo = static_cast<Gdiplus::ImageCodecInfo*>(malloc(pathSize));
	if (!pImageCodecInfo) {
		return -1;
	}

	Gdiplus::GetImageEncoders(num, pathSize, pImageCodecInfo);

#pragma warning(push)
#pragma warning(disable: 6385)
	for (UINT j = 0; j < num; ++j) {
		if (_tcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
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
BOOL SavePNGToFile(HGLOBAL hData, LPCTSTR filename)
{
	if (!hData or !filename) { return FALSE; }

	// Handle PNG data directly
	LPVOID pngData = GlobalLock(hData);
	BOOL success{};

	if (pngData) {
		SIZE_T dataSize = GlobalSize(hData);
		HANDLE hFile = CreateFile(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile != INVALID_HANDLE_VALUE) {
			DWORD bytesWritten;
			WriteFile(hFile, pngData, (DWORD)dataSize, &bytesWritten, NULL);
			CloseHandle(hFile);
			success = (bytesWritten == dataSize);
		}
		GlobalUnlock(hData);
	}
	return success;
}

// Function to save DIB to PNG file
BOOL SaveDIBToFile(HGLOBAL hData, LPCTSTR filename)
{
	if (!hData or !filename) { return FALSE; }

	// Lock the DIB to access its data
	BITMAPINFO* bmi = (BITMAPINFO*)GlobalLock(hData);
	if (!bmi) { return FALSE; }

	// Calculate the offset to the pixel data
	DWORD colorTableSize{};
	if (bmi->bmiHeader.biBitCount <= 8) {
		colorTableSize = (bmi->bmiHeader.biClrUsed ? bmi->bmiHeader.biClrUsed : (1 << bmi->bmiHeader.biBitCount)) * sizeof(RGBQUAD);
	}
	LPVOID pixels = (BYTE*)bmi + bmi->bmiHeader.biSize + colorTableSize;

	// Create a GDI+ Bitmap from the DIB
	Gdiplus::Bitmap bitmap(bmi, pixels);

	// Get the PNG encoder CLSID
	CLSID pngClsid;
	if (GetEncoderClsid(_T("image/png"), &pngClsid) < 0) {
		GlobalUnlock(hData);
		return FALSE;
	}

	// Save the bitmap as PNG
	Gdiplus::Status status = bitmap.Save(filename, &pngClsid, NULL);

	// Clean up
	GlobalUnlock(hData);
	return status == Gdiplus::Ok;
}

// Determines if the specified executable name is allowed based on a predefined list
BOOL CheckOwnerAllowed(LPCTSTR exeName)
{
	if (!exeName) { return FALSE; }

	return (_tcsicmp(exeName, _T("svchost.exe")) == 0);
}

// Retrieves the executable path of the clipboard owner process
LPCTSTR RetrieveClipboardOwner()
{
	static TCHAR exePath[MAX_PATH]; // Buffer for the executable path

	HWND hClipboardOwner = GetClipboardOwner();
	if (!hClipboardOwner) { return NULL; }

	DWORD processId{};
	GetWindowThreadProcessId(hClipboardOwner, &processId);
	if (!processId) { return NULL; }

	HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
	if (!hProcess) { return NULL; }

	DWORD pathSize = MAX_PATH;
	BOOL success = QueryFullProcessImageName(hProcess, 0, exePath, &pathSize);
	CloseHandle(hProcess);
	if (!success) { return NULL; }

	// Extract the executable name
	LPCTSTR exeName = _tcsrchr(exePath, _T('\\'));
	if (!exeName) { return NULL; }
	++exeName; // Move past the backslash

	return exeName;
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

	static UINT formatPriorityList[]{ CF_PNG, CF_DIBV5, CF_DIB, CF_BITMAP };

	HGLOBAL hClipboardData{};
	HGLOBAL hCopy{};
	*pFormat = 0;

	INT nFormat = GetPriorityClipboardFormat(formatPriorityList, _countof(formatPriorityList));
	if (nFormat) {
		hClipboardData = GetClipboardData(nFormat);
		if (hClipboardData) {
			SIZE_T dataSize = GlobalSize(hClipboardData);
			hCopy = GlobalAlloc(GMEM_MOVEABLE, dataSize);
			if (hCopy) {
				LPVOID pSrc = GlobalLock(hClipboardData);
				LPVOID pDest = GlobalLock(hCopy);
				if (pSrc && pDest) {
					memcpy(pDest, pSrc, dataSize);
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
ClipboardResult HandleClipboardData(LPTSTR sFormat, UINT cChFormat)
{
	static DWORD lastDataHash{};
	static SIZE_T lastDataSize{};

	if (!sFormat) { return ClipboardResult::InvalidParameter; }

	INT nFormat{};
	HGLOBAL hClipboardData = GetClipboardImageData(&nFormat);

	_tcscpy_s(sFormat, cChFormat, [&]() {
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

	LPBYTE pData = static_cast<LPBYTE>(GlobalLock(hClipboardData));
	if (!pData) {
		GlobalFree(hClipboardData);
		return ClipboardResult::LockFailed;
	}

	// Calculate content hash
	const SIZE_T dataSize = GlobalSize(hClipboardData);
	const DWORD dataHash = ComputeDataHash(pData, dataSize);
	GlobalUnlock(hClipboardData);

	// Check for duplicate content
	if (dataSize == lastDataSize && dataHash == lastDataHash) {
		GlobalFree(hClipboardData);
		return ClipboardResult::UnchangedContent;
	}

	// Generate filename and save
	LPCTSTR sFilename = GenerateFilename();
	if (!sFilename) {
		GlobalFree(hClipboardData);
		return ClipboardResult::SaveFailed;
	}

	BOOL result{};
	if (nFormat == CF_PNG) {
		result = SavePNGToFile(hClipboardData, sFilename);
	}
	else {
		result = SaveDIBToFile(hClipboardData, sFilename);
	}

	GlobalFree(hClipboardData);

	if (!result) {
		return ClipboardResult::SaveFailed;
	}

	// Update state only after successful save
	lastDataHash = dataHash;
	lastDataSize = dataSize;

	return ClipboardResult::Success;
}

// Tray Icon initialization
BOOL InitializeNotificationIcon(NOTIFYICONDATA* pNotifyIconData, HWND hWnd, HICON* hIcon)
{
	if (!pNotifyIconData or !hWnd or !hIcon) { return FALSE; }

	// Load the icon
	*hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_MAIN_ICON));
	if (!*hIcon) { return FALSE; }

	// Initialize NOTIFYICONDATA with modern settings
	pNotifyIconData->cbSize = sizeof(NOTIFYICONDATA); // Use full structure size
	pNotifyIconData->hWnd = hWnd;
	pNotifyIconData->uID = IDI_NOTIFY_ICON;
	pNotifyIconData->uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP | NIF_INFO; // Modern flags
	pNotifyIconData->uCallbackMessage = WM_APP_TRAYICON;
	pNotifyIconData->hIcon = *hIcon;
	pNotifyIconData->dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;  // For custom icons
	_tcscpy_s(pNotifyIconData->szTip, g_mainName);
	// Required for Windows 10/11
	pNotifyIconData->dwState = NIS_SHAREDICON;
	pNotifyIconData->dwStateMask = NIS_SHAREDICON;
	// First add the icon to the tray
	if (!Shell_NotifyIcon(NIM_ADD, pNotifyIconData)) {
		return FALSE;
	}
	// Set version AFTER adding
	pNotifyIconData->uVersion = NOTIFYICON_VERSION_4; // Use modern features
	//g_notifyIconData.uTimeout = 1000;  // 10 seconds (max allowed)
	return Shell_NotifyIcon(NIM_SETVERSION, pNotifyIconData);
}

// Initializes the GDI+ library for image processing
BOOL InitializeGDIPlus(ULONG_PTR* gdiplusToken)
{
	if (!gdiplusToken) { return FALSE; }

	Gdiplus::GdiplusStartupInput startupInput;
	return Gdiplus::GdiplusStartup(gdiplusToken, &startupInput, NULL) == Gdiplus::Status::Ok;
}

// Creates a popup menu for the system tray
BOOL CreateTrayPopupMenu(HMENU* hTrayContextMenu)
{
	if (!hTrayContextMenu) { return FALSE; }

	*hTrayContextMenu = CreatePopupMenu();
	if (!*hTrayContextMenu) { return FALSE; }

	AppendMenu(*hTrayContextMenu, MF_STRING, IDM_TRAY_OPEN_FOLDER,
		_T("Open folder")
	);
	AppendMenu(*hTrayContextMenu, MF_SEPARATOR, IDM_TRAY_SEPARATOR, NULL);
	AppendMenu(*hTrayContextMenu,
		MF_STRING | (g_settings.restrictToSystem ? MF_CHECKED : MF_UNCHECKED),
		IDM_TRAY_RESTRICT_TO_SYSTEM, _T("Restrict to System")
	);
	AppendMenu(*hTrayContextMenu,
		MF_STRING | (g_settings.showNotifications ? MF_CHECKED : MF_UNCHECKED),
		IDM_TRAY_SHOW_BALLOON, _T("Show notifications")
	);
	AppendMenu(*hTrayContextMenu, MF_SEPARATOR, IDM_TRAY_SEPARATOR, NULL);
	AppendMenu(*hTrayContextMenu, MF_STRING, IDM_TRAY_EXIT,
		_T("Exit")
	);

	return *hTrayContextMenu != NULL;
}

// Checks if the required time has elapsed since the last trigger time
BOOL IsTimeElapsed(ULONGLONG requiredElapsedTime)
{
	static ULONGLONG previousTriggerTime{};
	ULONGLONG currentSystemTime = GetTickCount64();

	if (currentSystemTime < requiredElapsedTime + previousTriggerTime) {
		return FALSE;
	}
	previousTriggerTime = currentSystemTime;

	return TRUE;
}

// Attempts to open the clipboard with retry logic on access denial
BOOL TryOpenClipboard()
{
	BOOL result = OpenClipboard(NULL);
	if (result) {
		return TRUE;
	}

	DWORD dwError = GetLastError();
	if (dwError == ERROR_ACCESS_DENIED) { // Clipboard is being held by another process
		INT retries = 4;
		UINT delay = 50;  // ms

		// Retry logic with progressive backoff
		do {
			Sleep(delay);
			delay *= 2;  // Exponential backoff
		} while (retries-- > 0 and !(result = OpenClipboard(NULL)));
	}
	return result;
}


// Window procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	// Variable to track exit code
	static INT exitCode{}; // Default: success

	static HICON hIcon{};
	static ULONG_PTR gdiplusToken{};
	static NOTIFYICONDATA notifyIconData{};
	static HMENU hTrayContextMenu{};


	switch (uMsg)
	{

	case WM_CLIPBOARDUPDATE:
	{
		static const UINT maxFormatStringLength = 64;
		static TCHAR clipboardFormatBuffer[maxFormatStringLength];

		// Debounce
		const ULONGLONG minTimeBetweenUpdates = 50; // 50ms
		if (!IsTimeElapsed(minTimeBetweenUpdates)) {
			break;
		}

		if (!TryOpenClipboard()) {
			ShowBalloonNotification(
				&notifyIconData, NIIF_ERROR,
				_T("Clipboard Error"), _T("Failed to access clipboard.\n%s"),
				ErrorToText(GetLastError())
			);
			break;
		}

		// Retrieves the name of the clipboard data owner
		LPCTSTR clipboardOwner = RetrieveClipboardOwner();
		if (!clipboardOwner) {
			CloseClipboard();
			break;
		}

		// Checks if the 'Restrict to System' option is enabled
		if (g_settings.restrictToSystem == TRUE) {
			if (!CheckOwnerAllowed(clipboardOwner)) {
				CloseClipboard();
				break;
			}
		}

		const BOOL isNotificationsAllowed = (GetMenuState(hTrayContextMenu, IDM_TRAY_SHOW_BALLOON, MF_BYCOMMAND) & MF_CHECKED) == MF_CHECKED;

		// Helper function for error cases
		const auto HandleClipboardError = [&](LPCTSTR title, LPCTSTR message) {
			if (isNotificationsAllowed) {
				ShowBalloonNotification(
					&notifyIconData, NIIF_ERROR,
					title, _T("%s.\n%s"),
					message, ErrorToText(GetLastError())
				);
			}
			CloseClipboard();
			exitCode = -1;
			DestroyWindow(hWnd);
			};

		// Helper function for success notifications
		const auto ShowStatusNotification = [&](LPCTSTR title) {
			if (isNotificationsAllowed) {
				ShowBalloonNotification(
					&notifyIconData, NIIF_INFO,
					title, _T("Owner:  %s\nType:  %s"),
					clipboardOwner, clipboardFormatBuffer
				);
			}
			};

		switch (HandleClipboardData(clipboardFormatBuffer, maxFormatStringLength))
		{
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

		default: break;
		}

		CloseClipboard();
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
			POINT cursorPosition;
			GetCursorPos(&cursorPosition);
			SetForegroundWindow(hWnd);
			TrackPopupMenuEx(hTrayContextMenu, TPM_RIGHTBUTTON, cursorPosition.x, cursorPosition.y, hWnd, NULL);
			break;
		}
		default: break;
		}
		break;
	}

	case WM_COMMAND:
	{
		if (HIWORD(wParam) == MENU)
		{
			if (LOWORD(wParam) == IDM_TRAY_EXIT) {
				SendMessage(hWnd, WM_CLOSE, 0, 0);
				break;
			}

			if (LOWORD(wParam) == IDM_TRAY_OPEN_FOLDER) {
				TCHAR dirPath[MAX_PATH]{};
				GetCurrentDirectory(MAX_PATH, dirPath);

				if (!OpenFolderInExplorer(dirPath)) {
					ShowBalloonNotification(
						&notifyIconData, NIIF_WARNING,
						_T("Explorer Error"), _T("Failed to open folder in File Explorer.\n%s"),
						ErrorToText(GetLastError())
					);
				}
				break;
			}

			if (LOWORD(wParam) == IDM_TRAY_RESTRICT_TO_SYSTEM) {
				bool isChecked = !!(GetMenuState(hTrayContextMenu, IDM_TRAY_RESTRICT_TO_SYSTEM, MF_BYCOMMAND) & MF_CHECKED);
				// Switch checkbox
				CheckMenuItem(hTrayContextMenu, IDM_TRAY_RESTRICT_TO_SYSTEM,
					MF_BYCOMMAND | (isChecked ? MF_UNCHECKED : MF_CHECKED)
				);
				if (IsFileExists(g_settings.iniPath)) {
					WriteIniInt(
						IniConfig::GENERAL, IniConfig::General::RESTRICT_TO_SYSTEM,
						!isChecked, g_settings.iniPath
					);
				}
				break;
			}

			if (LOWORD(wParam) == IDM_TRAY_SHOW_BALLOON) {
				bool isChecked = !!(GetMenuState(hTrayContextMenu, IDM_TRAY_SHOW_BALLOON, MF_BYCOMMAND) & MF_CHECKED);
				// Switch checkbox
				CheckMenuItem(hTrayContextMenu, IDM_TRAY_SHOW_BALLOON,
					MF_BYCOMMAND | (isChecked ? MF_UNCHECKED : MF_CHECKED)
				);
				if (IsFileExists(g_settings.iniPath)) {
					WriteIniInt(
						IniConfig::NOTIFICATIONS, IniConfig::Notifications::ENABLED,
						!isChecked, g_settings.iniPath
					);
				}
				break;
			}
			break;
		}

		if (HIWORD(wParam) == CONTROL) {
			if (LOWORD(wParam) == IDC_APP_MULTIPLE_INSTANCES) {
				ShowBalloonNotification(
					&notifyIconData, NIIF_WARNING,
					_T("Multiple Instances Warning"), _T("Another instance of this application is already running.")
				);
				break;
			}
			break;
		}

		break;
	}

	case WM_CREATE:
	{
		if (!InitializeGDIPlus(&gdiplusToken)) {
			ShowMessageBoxNotification(
				hWnd, MB_OK | MB_ICONERROR,
				_T("Initialization Error"), _T("Failed to initialize GDI+ library.\n%s"),
				ErrorToText(GetLastError())
			);
			return -1;
		}

		if (!InitializeNotificationIcon(&notifyIconData, hWnd, &hIcon)) {
			ShowMessageBoxNotification(
				hWnd, MB_OK | MB_ICONERROR,
				_T("Tray Icon Error"), _T("Failed to initialize system tray icon.\n%s"),
				ErrorToText(GetLastError())
			);
			return -1;
		}

		if (!CreateTrayPopupMenu(&hTrayContextMenu)) {
			ShowBalloonNotification(
				&notifyIconData, NIIF_ERROR,
				_T("Tray Menu Error"), _T("Failed to create system tray context menu.\n%s"),
				ErrorToText(GetLastError())
			);
			return -1;
		}

		if (!AddClipboardFormatListener(hWnd)) {
			ShowBalloonNotification(
				&notifyIconData, NIIF_ERROR,
				_T("Clipboard Error"), _T("Failed to register clipboard listener.\n%s"),
				ErrorToText(GetLastError())
			);
			return -1;
		}

		if (!CF_PNG) {
			ShowBalloonNotification(
				&notifyIconData, NIIF_WARNING,
				_T("Clipboard Format Error"), _T("PNG format not available.")
			);
		}

		HRESULT hrTheme = FollowSystemTheme(hWnd);
		if (hrTheme != S_OK) {
			ShowBalloonNotification(
				&notifyIconData, NIIF_WARNING,
				_T("Theme Error"), _T("Failed to apply system theme.\n%s"),
				hrTheme
			);
		}

		break;
	}

	case WM_DESTROY:
	{
		// Clean up system tray context menu
		if (hTrayContextMenu) {
			DestroyMenu(hTrayContextMenu);
			hTrayContextMenu = NULL;
		}

		// Remove system tray icon
		Shell_NotifyIcon(NIM_DELETE, &notifyIconData);
		notifyIconData = {};

		// Release tray icon resources
		if (hIcon) {
			DestroyIcon(hIcon);
			hIcon = NULL;
		}

		// Shutdown GDI+ if initialized
		if (gdiplusToken) {
			Gdiplus::GdiplusShutdown(gdiplusToken);
			gdiplusToken = NULL;
		}

		PostQuitMessage(exitCode);
		break;
	}

	case WM_SETTINGCHANGE:
	{
		if (lParam and
			CompareStringOrdinal(
				reinterpret_cast<LPCWCH>(lParam), -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL
			) {
			// Re-apply the correct theme based on current system setting
			FollowSystemTheme(hWnd);

			// Redraw window to reflect theme changes immediately
			//InvalidateRect(hWnd, nullptr, TRUE);
		}
		break;
	}

	default:
		break;
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}


// Main Application Entry Point
int _tmain(int argc, LPTSTR argv[])
{
	// Create a mutex with no security attributes
	g_hMutex = CreateMutex(NULL, TRUE, MUTEX_NAME);
	if (!g_hMutex) {
		ShowMessageBoxNotification(
			NULL, MB_OK | MB_ICONERROR,
			_T("Mutex operation failed"), _T("Unable to acquire lock.\n%s"),
			ErrorToText(GetLastError())
		);
		return 1;
	}

	DWORD mutexErrorCode = GetLastError();
	// If the mutex already exists, another instance is running
	if (mutexErrorCode == ERROR_ALREADY_EXISTS) {
		g_hMainWnd = FindWindow(g_mainClassName, NULL);
		if (g_hMainWnd) {
			PostMessage(g_hMainWnd, WM_COMMAND, MAKEWPARAM(IDC_APP_MULTIPLE_INSTANCES, CONTROL), (LPARAM)NULL);
		}
		return 0;
	}

	// Retrieve the INI file path (filename same as executable)
	if (!GetIniFilePath(g_settings.iniPath, MAX_PATH)) {
		ShowMessageBoxNotification(
			NULL, MB_OK | MB_ICONERROR,
			_T("Error Retrieving INI File"), _T("Failed to get INI file path.\n%s"),
			ErrorToText(GetLastError())
		);
		return 1;
	}

	// Read settings if the file exists, or set defaults
	InitializeDefaultSettings();

	// Initializes dark mode compatibility for the application
	EnableDarkModeSupport();

	// Create hidden window
	g_wcex = { sizeof(WNDCLASSEX) };
	g_wcex.lpfnWndProc = WndProc;
	g_wcex.hInstance = GetModuleHandle(NULL);
	g_wcex.lpszClassName = g_mainClassName;
	if (!RegisterClassEx(&g_wcex)) {
		ShowMessageBoxNotification(
			NULL, MB_OK | MB_ICONERROR,
			_T("Window Class Registration Failed"), _T("Unable to register window class.\n%s"),
			ErrorToText(GetLastError())
		);
		return 1;
	}

	g_hMainWnd = CreateWindowEx(
		0,
		g_wcex.lpszClassName,
		NULL,
		WS_POPUP,  // Invisible but valid parent
		0, 0, 0, 0,
		NULL, //HWND_MESSAGE,
		NULL, NULL, NULL
	);
	if (!g_hMainWnd) {
		ShowMessageBoxNotification(
			NULL, MB_OK | MB_ICONERROR,
			_T("Window Creation Failed"), _T("Unable to create main window.\n%s"),
			ErrorToText(GetLastError())
		);
		if (g_hMutex) {
			ReleaseMutex(g_hMutex);
			CloseHandle(g_hMutex);
		}
		return 1;
	}

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	// Cleanup
	if (g_hMainWnd) {
		RemoveClipboardFormatListener(g_hMainWnd);
		DestroyWindow(g_hMainWnd);
		UnregisterClass(g_wcex.lpszClassName, GetModuleHandle(NULL));
	}
	if (g_hMutex) {
		ReleaseMutex(g_hMutex);
		CloseHandle(g_hMutex);
	}

	return 0;
}



