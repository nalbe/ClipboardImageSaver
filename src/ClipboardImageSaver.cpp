#pragma once
#define _CRT_SECURE_NO_WARNINGS

#include "resource.h"
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <time.h>
#include <strsafe.h>
#include <gdiplus.h>
#include <shellapi.h> // For Shell_NotifyIcon
#include "murmurhash3.h" // Hash function
#pragma comment(lib, "gdiplus.lib")
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:wmainCRTStartup")


// Define message source codes
#define MENU            (0)
#define ACCELERATOR     (1)
#define CONTROL         (2)

// Define custom window message IDs
#define WM_APP_TRAYICON                (WM_APP + 1)

// Define custom icon IDs
#define IDI_NOTIFY_ICON                (WM_APP + 1)

// Define custom tray message IDs
#define IDM_TRAY_SHOW_BALLOON          (WM_APP + 5)
#define IDM_TRAY_RESTRICT_TO_SYSTEM    (WM_APP + 4)
#define IDM_TRAY_OPEN_FOLDER           (WM_APP + 3)
#define IDM_TRAY_EXIT                  (WM_APP + 2)
#define IDM_TRAY_SEPARATOR             (WM_APP + 1)

#ifndef PNG
// Register clipboard format for PNG
static UINT CF_PNG = RegisterClipboardFormat(_T("PNG"));
#endif

LPCTSTR g_mainName = _T("Clipboard Image Saver");
LPCTSTR g_mainClassName = _T("CISClassname");
LPCTSTR MUTEX_NAME = _T("CISInstance");

HWND g_hMainWnd{};
HANDLE g_hMutex{};
WNDCLASSEXW g_wcex{};


// Defines possible outcomes of clipboard data processing
enum class ClipboardResult
{
	Success,            // Data processed and saved
	NoData,             // No valid image data in clipboard
	ConversionFailed,   // CF_BITMAP to CF_DIB conversion failed
	LockFailed,         // Failed to lock clipboard data
	UnchangedContent,   // Same content as previous capture
	SaveFailed          // File save operation failed
};

// Define notification variants using enum class for type safety
enum class NotificationVariant {
	Info,       // Standard notification
	Warning,    // Important warning
	Error,      // Critical error
	Custom_OK,  // Custom message with parameters
	Custom_NOT  // Custom message with parameters
};


inline DWORD ShowError(LPCTSTR sMessage, DWORD nError = ERROR_SUCCESS)
{
	TCHAR buffer[256]{};

	if (nError != ERROR_SUCCESS) {
		_stprintf_s(buffer, _countof(buffer), _T("%s ( %lu )"), sMessage, nError);
	}
	else {
		_tcscpy_s(buffer, _countof(buffer), sMessage);
	}

	MessageBox(NULL, buffer, _T("Error"), MB_OK | MB_ICONERROR);
	return nError;
}

// Opens the specified folder in Windows Explorer.
BOOL OpenFolderInExplorer(LPCTSTR folderPath)
{
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

// Fast 32-bit hash function (for binary data)
UINT32 ComputeDataHash(LPCBYTE pData, SIZE_T dataSize)
{
	UINT32 seed = 0; // Fixed seed for consistency
	UINT32 hash = MurmurHash3_32(pData, dataSize, seed);
	return hash;
}

// Generates a filename string
LPTSTR GenerateFilename()
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
			return buffer;
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
	UINT num{};
	UINT pathSize{};

	Gdiplus::GetImageEncodersSize(&num, &pathSize);
	if (num == 0 || pathSize == 0) {
		return -1;
	}

	Gdiplus::ImageCodecInfo* pImageCodecInfo = static_cast<Gdiplus::ImageCodecInfo*>(malloc(pathSize));
	if (!pImageCodecInfo) {
		return -1;
	}

	Gdiplus::GetImageEncoders(num, pathSize, pImageCodecInfo);

	// Calculate the maximum number of entries based on buffer pathSize
	UINT maxEntries = pathSize / sizeof(Gdiplus::ImageCodecInfo);
	UINT entriesToCheck = (num < maxEntries) ? num : maxEntries;

	for (UINT j{}; j < entriesToCheck; ++j) {
		if (_tcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
			*pClsid = pImageCodecInfo[j].Clsid;
			free(pImageCodecInfo);
			return j;
		}
	}

	free(pImageCodecInfo);
	return -1;
}

// Function to save PNG to file
BOOL SavePNGToFile(HGLOBAL hData, LPCTSTR filename)
{
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
	if (!hData) { return FALSE; }

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

// Checks if the executable is allowed
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
	LPTSTR sFilename = GenerateFilename();
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
	Gdiplus::GdiplusStartupInput startupInput;
	return Gdiplus::GdiplusStartup(gdiplusToken, &startupInput, NULL) == Gdiplus::Status::Ok;
}

// Creates a popup menu for the system tray
BOOL CreateTrayPopupMenu(HMENU* hTrayContextMenu)
{
	if (!hTrayContextMenu) { return FALSE; }

	*hTrayContextMenu = CreatePopupMenu();
	if (!*hTrayContextMenu) {
		return FALSE;
	}

	AppendMenu(*hTrayContextMenu, MF_STRING, IDM_TRAY_OPEN_FOLDER,
		_T("Open folder")
	);
	AppendMenu(*hTrayContextMenu, MF_SEPARATOR, IDM_TRAY_SEPARATOR, NULL);
	AppendMenu(*hTrayContextMenu, MF_STRING | MF_CHECKED, IDM_TRAY_RESTRICT_TO_SYSTEM,
		_T("Restrict to System")
	);
	AppendMenu(*hTrayContextMenu, MF_STRING | MF_CHECKED, IDM_TRAY_SHOW_BALLOON,
		_T("Show notifications")
	);
	AppendMenu(*hTrayContextMenu, MF_SEPARATOR, IDM_TRAY_SEPARATOR, NULL);
	AppendMenu(*hTrayContextMenu, MF_STRING, IDM_TRAY_EXIT,
		_T("Exit")
	);
	AppendMenu(*hTrayContextMenu, MF_SEPARATOR, IDM_TRAY_SEPARATOR, NULL);

	return *hTrayContextMenu != NULL;
}

// Notification function
BOOL ShowBalloonNotification(NOTIFYICONDATA* pNotifyIconData, NotificationVariant variant,
	LPCTSTR sHeader, LPCTSTR sMessage = NULL, LPCTSTR sDetails = NULL
){
	if (!pNotifyIconData->hWnd) { return FALSE; }

	pNotifyIconData->dwInfoFlags = [&]() {
		switch (variant) {
		case NotificationVariant::Warning:  return NIIF_WARNING;
		case NotificationVariant::Error:    return NIIF_ERROR;
		default:                            return NIIF_INFO;
		}
	}();

	_tcscpy_s(
		pNotifyIconData->szInfoTitle,
		[&]() {
			switch (variant) {
			case NotificationVariant::Custom_OK:   return _T("Clipboard Data Captured");
			case NotificationVariant::Custom_NOT:  return _T("Clipboard Data Ignored");
			default:                               return sHeader ? sHeader : _T("");
			}
		}()
	);

	_stprintf_s(
		pNotifyIconData->szInfo,
		[&]() {
			switch (variant) {
			case NotificationVariant::Custom_OK:   return _T("Owner:  %s\nType:  %s");
			case NotificationVariant::Custom_NOT:  return _T("Owner:  %s\nType:  %s\nDuplicate sequence data detected");
			default:                               return _T("%s\n%s");
			}
		}(),
		sMessage ? sMessage : _T(""),
		sDetails ? sDetails : _T("")
	);

	return Shell_NotifyIcon(NIM_MODIFY, pNotifyIconData) == TRUE;
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

	DWORD err = GetLastError();
	if (err == ERROR_ACCESS_DENIED) { // Clipboard is being held by another process
		// Retry logic with progressive backoff
		INT retries = 4;
		UINT delay = 50;  // ms

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
	// Data for the system tray icon
	static HICON hIcon{};
	static ULONG_PTR gdiplusToken{};
	static NOTIFYICONDATA notifyIconData{};
	static HMENU hTrayContextMenu{};


	switch (uMsg)
	{

	case WM_CLIPBOARDUPDATE:
	{
		// Debounce
		const ULONGLONG minTimeBetweenUpdates = 50; // 50ms
		if (!IsTimeElapsed(minTimeBetweenUpdates)) {
			break;
		}

		if (!TryOpenClipboard()) {
			ShowBalloonNotification(
				&notifyIconData,
				NotificationVariant::Error,
				_T("Clipboard Error"),
				_T("Failed to access clipboard")
			);
			break;
		}

		// Retrieves the name of the clipboard data owner
		LPCTSTR clipboardOwner = RetrieveClipboardOwner();
		if (!clipboardOwner) {
			CloseClipboard();
			break;
		}

		// Checks if the 'Restrict to System' option is enabled in the tray menu
		if ((GetMenuState(hTrayContextMenu, IDM_TRAY_RESTRICT_TO_SYSTEM, MF_BYCOMMAND) & MF_CHECKED) == MF_CHECKED) {
			if (!CheckOwnerAllowed(clipboardOwner)) {
				CloseClipboard();
				break;
			}
		}

		const UINT maxFormatStringLength = 64;
		TCHAR clipboardFormatBuffer[maxFormatStringLength];
		const BOOL isNotificationsAllowed = (GetMenuState(hTrayContextMenu, IDM_TRAY_SHOW_BALLOON, MF_BYCOMMAND) & MF_CHECKED) == MF_CHECKED;

		// Helper function for error cases
		const auto HandleClipboardError = [&](LPCTSTR title, LPCTSTR message) {
			if (isNotificationsAllowed) {
				ShowBalloonNotification(&notifyIconData, NotificationVariant::Error, title, message);
			}
			CloseClipboard();
			PostQuitMessage(-1);
			};

		// Helper function for success notifications
		const auto ShowStatusNotification = [&](NotificationVariant variant) {
			if (isNotificationsAllowed) {
				ShowBalloonNotification(&notifyIconData, variant, _T(""), clipboardOwner, clipboardFormatBuffer);
			}
			};

		switch (HandleClipboardData(clipboardFormatBuffer, maxFormatStringLength))
		{
		case ClipboardResult::Success:
			ShowStatusNotification(NotificationVariant::Custom_OK);
			break;

		case ClipboardResult::UnchangedContent:
			//ShowStatusNotification(NotificationVariant::Custom_NOT);
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
			TrackPopupMenu(hTrayContextMenu, TPM_LEFTBUTTON, cursorPosition.x, cursorPosition.y, 0, hWnd, NULL);
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
						&notifyIconData,
						NotificationVariant::Warning,
						_T("Explorer Error"),
						_T("Failed to open folder in File Explorer")
					);
				}
				break;
			}

			if (LOWORD(wParam) == IDM_TRAY_RESTRICT_TO_SYSTEM) {
				// Switch checkbox
				UINT menuItemState = GetMenuState(hTrayContextMenu, IDM_TRAY_RESTRICT_TO_SYSTEM, MF_BYCOMMAND);
				CheckMenuItem(hTrayContextMenu, IDM_TRAY_RESTRICT_TO_SYSTEM, MF_BYCOMMAND | ((menuItemState & MF_CHECKED) ? MF_UNCHECKED : MF_CHECKED));
				break;
			}

			if (LOWORD(wParam) == IDM_TRAY_SHOW_BALLOON) {
				// Switch checkbox
				UINT menuItemState = GetMenuState(hTrayContextMenu, IDM_TRAY_SHOW_BALLOON, MF_BYCOMMAND);
				CheckMenuItem(hTrayContextMenu, IDM_TRAY_SHOW_BALLOON, MF_BYCOMMAND | ((menuItemState & MF_CHECKED) ? MF_UNCHECKED : MF_CHECKED));
				break;
			}
		}
		break;
	}

	case WM_CREATE:
	{
		if (!InitializeGDIPlus(&gdiplusToken)) {
			MessageBox(hWnd,
				_T("Failed to initialize GDI+ library"),
				_T("Initialization Error"),
				MB_OK | MB_ICONERROR
			);
			return -1;
		}

		if (!InitializeNotificationIcon(&notifyIconData, hWnd, &hIcon)) {
			MessageBox(hWnd,
				_T("Failed to initialize system tray icon"),
				_T("Tray Icon Error"),
				MB_OK | MB_ICONERROR
			);
			return -1;
		}

		if (!CreateTrayPopupMenu(&hTrayContextMenu)) {
			ShowBalloonNotification(
				&notifyIconData,
				NotificationVariant::Error,
				_T("Tray Menu Error"),
				_T("Failed to create system tray context menu")
			);
			return -1;
		}

		if (!AddClipboardFormatListener(hWnd)) {
			ShowBalloonNotification(
				&notifyIconData,
				NotificationVariant::Error,
				_T("Clipboard Error"),
				_T("Failed to register clipboard listener")
			);
			return -1;
		}

		if (!CF_PNG) {
			ShowBalloonNotification(
				&notifyIconData,
				NotificationVariant::Warning,
				_T("Clipboard Format Error"),
				_T("PNG format not available")
			);
		}

		break;
	}

	case WM_CLOSE:
	{
		DestroyWindow(g_hMainWnd);
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

		// Release tray icon resources
		if (hIcon) {
			DestroyIcon(hIcon);
			hIcon = NULL;
		}

		// Shutdown GDI+ if initialized
		if (gdiplusToken) {
			Gdiplus::GdiplusShutdown(gdiplusToken);
		}

		PostQuitMessage(0);
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
	// Single instance check
	g_hMutex = CreateMutex(NULL, TRUE, MUTEX_NAME);
	if (!g_hMutex) {
		ShowError(_T("Error: Failed to get mutex.\n"), GetLastError());
		return 1;
	}
	DWORD mutexResult = GetLastError();
	DWORD result{};

	if (mutexResult == ERROR_ALREADY_EXISTS) {
		return 0;
	}

	// Create hidden window
	g_wcex = { sizeof(WNDCLASSEX) };
	g_wcex.lpfnWndProc = WndProc;
	g_wcex.hInstance = GetModuleHandle(NULL);
	g_wcex.lpszClassName = g_mainClassName;
	if (!RegisterClassEx(&g_wcex)) {
		ShowError(_T("Error: Failed to register class.\n"), GetLastError());
		return 1;
	}

	g_hMainWnd = CreateWindowEx(
		0,
		g_wcex.lpszClassName,
		_T(""),
		0, 0, 0, 0, 0,
		HWND_MESSAGE,
		NULL, NULL, NULL
	);
	if (!g_hMainWnd) {
		ShowError(_T("Error: Failed to create window.\n"), GetLastError());
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



