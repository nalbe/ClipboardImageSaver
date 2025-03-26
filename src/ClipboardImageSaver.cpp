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
#define WM_TRAYICON                  (WM_APP + 1)

// Define custom window message IDs
#define IDI_NOTIFY_ICON              (WM_APP + 1)

// Define custom tray message IDs
#define IDM_SHOW_BALLOON             (WM_APP + 5)
#define IDM_RESTRICT_TO_SYSTEM       (WM_APP + 4)
#define IDM_OPEN_FOLDER              (WM_APP + 3)
#define IDM_TRAY_EXIT                (WM_APP + 2)
#define IDM_TRAY_SEPARATOR           (WM_APP + 1)


LPCTSTR g_mainName = _T("Clipboard Image Saver");
LPCTSTR g_mainClassName = _T("CISClassname");
LPCTSTR MUTEX_NAME = _T("CISInstance");

HWND g_hMainWnd{};
HANDLE g_hMutex{};
WNDCLASSEXW g_wcex{};
HMENU g_hTrayContextMenu{};
NOTIFYICONDATA g_notifyIconData{};

// Register clipboard format for PNG
static UINT CF_PNG = RegisterClipboardFormat(_T("PNG"));
// Global GDI+
ULONG_PTR g_gdiplusToken{};


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
	Custom      // Custom message with parameters
};


inline void Cleanup()
{
	if (g_gdiplusToken) {
		Gdiplus::GdiplusShutdown(g_gdiplusToken);
	}
	if (g_hMainWnd) {
		RemoveClipboardFormatListener(g_hMainWnd);
		DestroyWindow(g_hMainWnd);
		UnregisterClass(g_wcex.lpszClassName, GetModuleHandle(NULL));
	}
	if (g_hMutex) {
		ReleaseMutex(g_hMutex);
		CloseHandle(g_hMutex);
	}
}

inline void ForcedExit(LPCTSTR uMsg, DWORD error = ERROR_SUCCESS)
{
	TCHAR buffer[256]{};
	_ftprintf(stderr, _T("%s ( %lu )\n"), uMsg, error);
	MessageBox(NULL, buffer, _T("Error"), MB_OK | MB_ICONERROR);

	Cleanup();
	exit(error);
}

// 
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
UINT32 ComputeImageHash(LPCBYTE imageData, SIZE_T dataSize)
{
	UINT32 seed = 0; // Fixed seed for consistency
	UINT32 hash = MurmurHash3_32(imageData, dataSize, seed);
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
	if (!g_gdiplusToken) { return -1; }

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
HGLOBAL GetClipboardImageData(UINT* pFormat)
{
	HGLOBAL hClipboardData = NULL;
	HGLOBAL hCopy = NULL;
	*pFormat = 0;

	// Priority: PNG > DIBV5 > DIB > Bitmap
	UINT formats[] = { CF_PNG, CF_DIBV5, CF_DIB, CF_BITMAP };
	for (UINT fmt : formats) {
		if (IsClipboardFormatAvailable(fmt)) {
			hClipboardData = GetClipboardData(fmt);
			if (hClipboardData) {
				// Copy clipboard data to a new caller-owned handle
				SIZE_T dataSize = GlobalSize(hClipboardData);
				hCopy = GlobalAlloc(GMEM_MOVEABLE, dataSize);
				if (hCopy) {
					LPVOID pSrc = GlobalLock(hClipboardData);
					LPVOID pDest = GlobalLock(hCopy);
					if (pSrc && pDest) {
						memcpy(pDest, pSrc, dataSize);
						*pFormat = fmt;
					}
					GlobalUnlock(hClipboardData);
					GlobalUnlock(hCopy);
				}
				break;
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

	UINT format{};
	HGLOBAL hClipboardData = GetClipboardImageData(&format);
	if (!hClipboardData) {
		return ClipboardResult::NoData;
	}

	_tcscpy_s(sFormat, cChFormat, [&]() {
		if (format == CF_PNG)    return _T("CF_PNG");
		if (format == CF_DIBV5)  return _T("CF_DIBV5");
		if (format == CF_DIB)    return _T("CF_DIB");
		if (format == CF_BITMAP) return _T("CF_BITMAP");
		                    else return _T("Unknown");
		}()
	);

	// Convert CF_BITMAP to CF_DIB if needed
	if (format == CF_BITMAP) {
		if (!BitmapToDIB(hClipboardData)) {
			GlobalFree(hClipboardData);
			return ClipboardResult::ConversionFailed;
		}
		format = CF_DIB;
	}

	LPBYTE pData = static_cast<LPBYTE>(GlobalLock(hClipboardData));
	if (!pData) {
		GlobalFree(hClipboardData);
		return ClipboardResult::LockFailed;
	}

	// Calculate content hash
	const SIZE_T dataSize = GlobalSize(hClipboardData);
	const DWORD currentHash = ComputeImageHash(pData, dataSize);
	GlobalUnlock(hClipboardData);

	// Check for duplicate content
	if (dataSize == lastDataSize && currentHash == lastDataHash) {
		GlobalFree(hClipboardData);
		return ClipboardResult::UnchangedContent;
	}

	// Generate filename and save
	LPTSTR filename = GenerateFilename();
	BOOL result{};

	if (format == CF_PNG) {
		result = SavePNGToFile(hClipboardData, filename);
	}
	else {
		result = SaveDIBToFile(hClipboardData, filename);
	}

	GlobalFree(hClipboardData);

	if (!result) {
		return ClipboardResult::SaveFailed;
	}

	// Update state only after successful save
	lastDataHash = currentHash;
	lastDataSize = dataSize;

	return ClipboardResult::Success;
}

// Tray Icon initialization
BOOL InitializeNotificationIcon(HWND hWnd, HICON* hIcon)
{
	// Load the icon
	*hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_MAIN_ICON));
	if (!*hIcon) return FALSE;

	// Initialize NOTIFYICONDATA with modern settings
	g_notifyIconData.cbSize = sizeof(NOTIFYICONDATA); // Use full structure size
	g_notifyIconData.hWnd = hWnd;
	g_notifyIconData.uID = IDI_NOTIFY_ICON;
	g_notifyIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP | NIF_INFO; // Modern flags
	g_notifyIconData.uCallbackMessage = WM_TRAYICON;
	g_notifyIconData.hIcon = *hIcon;
	g_notifyIconData.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;  // For custom icons
	_tcscpy_s(g_notifyIconData.szTip, g_mainName);
	// Required for Windows 10/11
	g_notifyIconData.dwState = NIS_SHAREDICON;
	g_notifyIconData.dwStateMask = NIS_SHAREDICON;
	// First add the icon to the tray
	if (!Shell_NotifyIcon(NIM_ADD, &g_notifyIconData)) {
		return FALSE;
	}
	// Set version AFTER adding
	g_notifyIconData.uVersion = NOTIFYICON_VERSION_4; // Use modern features
	//g_notifyIconData.uTimeout = 1000;  // 10 seconds (max allowed)
	return Shell_NotifyIcon(NIM_SETVERSION, &g_notifyIconData);
}

// Initializes the GDI+ library for image processing
void InitializeGDIPlus() {
	Gdiplus::GdiplusStartupInput startupInput;
	Gdiplus::GdiplusStartup(&g_gdiplusToken, &startupInput, NULL);
}

// Creates a popup menu for the system tray
BOOL CreateTrayPopupMenu(HMENU* g_hTrayContextMenu)
{
	if (g_hTrayContextMenu) {
		*g_hTrayContextMenu = CreatePopupMenu();
		
		if (*g_hTrayContextMenu) {
			AppendMenu(*g_hTrayContextMenu, MF_STRING, IDM_OPEN_FOLDER,
				_T("Open folder")
			);
			AppendMenu(*g_hTrayContextMenu, MF_SEPARATOR, IDM_TRAY_SEPARATOR, NULL);
			AppendMenu(*g_hTrayContextMenu, MF_STRING | MF_CHECKED, IDM_RESTRICT_TO_SYSTEM,
				_T("Restrict to System")
			);
			AppendMenu(*g_hTrayContextMenu, MF_STRING | MF_CHECKED, IDM_SHOW_BALLOON,
				_T("Show notifications")
			);
			AppendMenu(*g_hTrayContextMenu, MF_SEPARATOR, IDM_TRAY_SEPARATOR, NULL);
			AppendMenu(*g_hTrayContextMenu, MF_STRING, IDM_TRAY_EXIT,
				_T("Exit")
			);
			AppendMenu(*g_hTrayContextMenu, MF_SEPARATOR, IDM_TRAY_SEPARATOR, NULL);
			return *g_hTrayContextMenu != NULL;
		}
	}
	return FALSE;
}

// Main notification function
void ShowBalloonNotification(NotificationVariant variant, LPCTSTR message = NULL, LPCTSTR details = NULL)
{
	if (!g_notifyIconData.hWnd) { return; }

	g_notifyIconData.dwInfoFlags = [&]() {
		switch (variant) {
		case NotificationVariant::Warning: return NIIF_WARNING;
		case NotificationVariant::Error:   return NIIF_ERROR;
		default:                           return NIIF_INFO;
		}
		}();

	_tcscpy_s(
		g_notifyIconData.szInfoTitle,
		[&]() {
			switch (variant) {
			case NotificationVariant::Info:    return _T("Clipboard Data Captured");
			case NotificationVariant::Custom:  return _T("Clipboard Data Ignored");
			default:                           return _T("");
			}
		}()
	);

	_stprintf_s(
		g_notifyIconData.szInfo,
		[&]() {
			switch (variant) {
			case NotificationVariant::Info:    return _T("owner:  %s\nformat:  %s");
			case NotificationVariant::Custom:  return _T("owner:  %s\nformat:  %s\nDuplicate sequence data detected");
			case NotificationVariant::Warning: return _T("Caution: %s\n%s");
			case NotificationVariant::Error:   return _T("Heads Up: %s\n%s");
			default:                           return _T("");
			}
		}(),
		message, details
	);

	Shell_NotifyIcon(NIM_MODIFY, &g_notifyIconData);
}


// Window procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	// Data for the system tray icon
	static HICON hIcon{};

	switch (uMsg)
	{

	case WM_CLIPBOARDUPDATE:
	{
		if (!OpenClipboard(hWnd)) {
			break;
		}

		// Retrieves the name of the clipboard data owner
		LPCTSTR clipboardOwner = RetrieveClipboardOwner();
		if (!clipboardOwner) {
			CloseClipboard();
			break;
		}

		// Checks if the 'Restrict to System' option is enabled in the tray menu
		if ((GetMenuState(g_hTrayContextMenu, IDM_RESTRICT_TO_SYSTEM, MF_BYCOMMAND) & MF_CHECKED) == MF_CHECKED) {
			if (!CheckOwnerAllowed(clipboardOwner)) {
				CloseClipboard();
				break;
			}
		}

		const UINT cChFormat = 64;
		TCHAR sFormat[cChFormat];
		// Attempts to process clipboard data
		switch (HandleClipboardData(sFormat, cChFormat)) {
		case ClipboardResult::Success:
		{
			// Displays a balloon notification if the 'Show Balloon' option is enabled
			if ((GetMenuState(g_hTrayContextMenu, IDM_SHOW_BALLOON, MF_BYCOMMAND) & MF_CHECKED) == MF_CHECKED) {
				ShowBalloonNotification(NotificationVariant::Info, clipboardOwner, sFormat);
			}
			break;
		}
		case ClipboardResult::UnchangedContent:
		{
			// Displays a balloon notification if the 'Show Balloon' option is enabled
			if ((GetMenuState(g_hTrayContextMenu, IDM_SHOW_BALLOON, MF_BYCOMMAND) & MF_CHECKED) == MF_CHECKED) {
				//ShowBalloonNotification(NotificationVariant::Custom, clipboardOwner, sFormat);
			}
			break;
		}
		default: break;
		}

		CloseClipboard();
		break;
	}

	case WM_TRAYICON:
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
			TrackPopupMenu(g_hTrayContextMenu, TPM_LEFTBUTTON, cursorPosition.x, cursorPosition.y, 0, hWnd, NULL);
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
			}

			else if (LOWORD(wParam) == IDM_OPEN_FOLDER) {
				TCHAR dirPath[MAX_PATH]{};
				GetCurrentDirectory(MAX_PATH, dirPath);

				if (!OpenFolderInExplorer(dirPath)) {
					MessageBox(NULL, _T("Failed to Open Image Path."), _T("Error"), MB_OK | MB_ICONERROR);
				}
			}

			else if (LOWORD(wParam) == IDM_RESTRICT_TO_SYSTEM) {
				// Switch checkbox
				UINT menuItemState = GetMenuState(g_hTrayContextMenu, IDM_RESTRICT_TO_SYSTEM, MF_BYCOMMAND);
				CheckMenuItem(g_hTrayContextMenu, IDM_RESTRICT_TO_SYSTEM, MF_BYCOMMAND | ((menuItemState & MF_CHECKED) ? MF_UNCHECKED : MF_CHECKED));
			}
			else if (LOWORD(wParam) == IDM_SHOW_BALLOON) {
				// Switch checkbox
				UINT menuItemState = GetMenuState(g_hTrayContextMenu, IDM_SHOW_BALLOON, MF_BYCOMMAND);
				CheckMenuItem(g_hTrayContextMenu, IDM_SHOW_BALLOON, MF_BYCOMMAND | ((menuItemState & MF_CHECKED) ? MF_UNCHECKED : MF_CHECKED));
			}
		}
		break;
	}

	case WM_CREATE:
	{
		InitializeNotificationIcon(hWnd, &hIcon);
		CreateTrayPopupMenu(&g_hTrayContextMenu);
		break;
	}

	case WM_CLOSE:
	{
		DestroyWindow(g_hMainWnd);
		break;
	}

	case WM_DESTROY:
	{
		Shell_NotifyIcon(NIM_DELETE, &g_notifyIconData);
		// Delete notification icon
		if (hIcon) {
			DestroyIcon(hIcon);
			hIcon = NULL;
		}
		// Destroy context menu
		if (g_hTrayContextMenu) {
			DestroyMenu(g_hTrayContextMenu);
			g_hTrayContextMenu = NULL;
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
		ForcedExit(_T("Error: Failed to get mutex.\n"), GetLastError());
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
		ForcedExit(_T("Error: Failed to register class.\n"), GetLastError());
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
		ForcedExit(_T("Error: Failed to create window.\n"), GetLastError());
	}

	InitializeGDIPlus();

	// Clipboard listener
	if (!AddClipboardFormatListener(g_hMainWnd)) {
		ForcedExit(_T("Error: Failed add clipboard listener.\n"), GetLastError());
	}

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

	Cleanup();
	return 0;
}



