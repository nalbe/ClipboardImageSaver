// Implementation-specific headers
#include "EditDialog.h"
#include "AppDefine.h"
#include "CustomIncludes\WinApi\ThemeManager.h"  // Dark theme support

// Windows system headers
#include <commctrl.h>    // Modern controls

// Library links
#pragma comment(lib, "Comctl32.lib")



LPCWSTR DIALOG_CLASSNAME  = _T("CISWhitelistEditClass");
LPCWSTR DIALOG_NAME       = _T("Whitelist Edit");


// Anonymous namespace for internal globals
namespace
{ 
	// Constants for theming
	constexpr COLORREF kDarkBg      = 0x202020;  // Dark background (RGB: 32, 32, 32)
	constexpr COLORREF kDarkText    = 0xE0E0E0;  // Dark theme text (RGB: 224, 224, 224)
	constexpr COLORREF kDarkBorder  = 0x404040;  // Dark theme border (RGB: 64, 64, 64)
	constexpr COLORREF kLightBg     = 0xFFFFFF;  // Light background (RGB: 255, 255, 255)
	constexpr COLORREF kLightText   = 0x000000;  // Light theme text (RGB: 0, 0, 0)
	constexpr COLORREF kLightBorder = 0xD4D4D4;  // Light theme border (RGB: 212, 212, 212)

	// Global window handles
	HWND g_hDialog{};
	HWND g_hTooltip{};
}



// Dialog proc
LRESULT CALLBACK DialogProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static HBRUSH hBgBrush{};     // Brush for background
	static HBRUSH hFlashBrush{};  // Brush for error state highlighting
	static BOOL isDarkTheme{};    // Indicates if dark theme is currently active
	static BOOL isFlashing{};     // Indicates if error state flashing is active


	switch (uMsg)
	{
	case WM_COMMAND:
	{
		if (LOWORD(wParam) == IDOK) {
			// Retrieve parameters from GWLP_USERDATA
			PDialogParams pParams = reinterpret_cast<PDialogParams>(
				GetWindowLongPtr(hWnd, GWLP_USERDATA));

			// Copy dialog text to buffer
			if (pParams and pParams->szBuffer) {
				GetDlgItemText(hWnd, IDC_EDIT, pParams->szBuffer, pParams->cchMax);
			}

			// Notify parent that edit is over
			BOOL bError = (BOOL)SendMessage(GetParent(hWnd), WM_APP_CUSTOM_MESSAGE,
				MAKEWPARAM(ID_DIALOG_RESULT, TRUE), (LPARAM)hWnd);

			// Invalid input case - keep dialog open and indicate error
			if (bError != ERROR_SUCCESS) {
				if (!hFlashBrush) {
					hFlashBrush = CreateSolidBrush(RGB(255, 0, 0));
				}
				isFlashing = TRUE;  // Start button flashing
				InvalidateRect(GetDlgItem(hWnd, IDOK), NULL, TRUE);  // Force redraw
				SetTimer(hWnd, IDT_DIALOG_ERROR_TIMER, 500, NULL);  // 500ms interval
				break;
			}

			DestroyWindow(hWnd);
			return TRUE;
		}
		else if (LOWORD(wParam) == IDCANCEL) {
			// Notify parent that edit is over
			BOOL bError = (BOOL)SendMessage(GetParent(hWnd), WM_APP_CUSTOM_MESSAGE,
				MAKEWPARAM(ID_DIALOG_RESULT, FALSE), (LPARAM)hWnd);

			DestroyWindow(hWnd);
			return TRUE;
		}
		break;
	}

	case WM_CTLCOLORBTN:
	{
		if (isFlashing and GetDlgCtrlID((HWND)lParam) == IDOK) {
			SetBkColor((HDC)wParam, RGB(200, 0, 0));
			return (LRESULT)hFlashBrush;
		}
		break;
	}

	case WM_CTLCOLOREDIT:
		[[fallthrough]]; // Intentional fallthrough

	case WM_CTLCOLORSTATIC:
	{
		if (isDarkTheme) {
			//SetBkMode((HDC)wParam, TRANSPARENT);  // Affect text redraw
			SetTextColor((HDC)wParam, kDarkText);
			SetBkColor((HDC)wParam, kDarkBg);
		}
		else {
			//SetBkMode((HDC)wParam, TRANSPARENT);  // Affect text redraw
			SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
			SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
		}
		return (LRESULT)hBgBrush;
	}

	case WM_CTLCOLORDLG:
	{
		return (INT_PTR)hBgBrush;
	}

	case WM_ERASEBKGND:
	{
		HDC hdc = (HDC)wParam;
		RECT rc;
		GetClientRect(hWnd, &rc);
		FillRect(hdc, &rc, hBgBrush);
		return TRUE; // Indicate background was handled
	}

	case WM_DESTROY:
	{
		if (hBgBrush) {
			DeleteObject(hBgBrush);
			hBgBrush = NULL;
		}
		if (hFlashBrush) {  // Fallback: if not deleted in WM_TIMER
			DeleteObject(hFlashBrush);
			hFlashBrush = NULL;
		}
		isDarkTheme = isFlashing = 0;
		g_hDialog = g_hTooltip = NULL;

		// Delete the heap-allocated struct
		PDialogParams pParams = reinterpret_cast<PDialogParams>(
			GetWindowLongPtr(hWnd, GWLP_USERDATA));
		if (pParams) {
			if (pParams->szBuffer) {
				delete[] pParams->szBuffer;  // Free buffer memory
			}
			delete pParams;
			SetWindowLongPtr(hWnd, GWLP_USERDATA, NULL); // Clear pointer
		}
		break;
	}

	case WM_APP_CUSTOM_MESSAGE:
	{
		if (LOWORD(wParam) == ID_THEME_CHANGED) {
			ThemeManager::FollowSystemTheme(hWnd, &isDarkTheme);
			ThemeManager::FollowSystemTheme(g_hTooltip);

			// Select and create the appropriate background brush based on theme
			if (hBgBrush) { DeleteObject(hBgBrush); }
			hBgBrush = CreateSolidBrush(isDarkTheme ? kDarkBg : GetSysColor(COLOR_WINDOW));
			
			return 0;
		}
		return 1;
	}

	case WM_TIMER:
	{
		if (wParam == IDT_DIALOG_ERROR_TIMER) {
			KillTimer(hWnd, IDT_DIALOG_ERROR_TIMER);  // Destroy the timer
			isFlashing = FALSE;  // Stop flashing
			if (hFlashBrush) {
				DeleteObject(hFlashBrush);
				hFlashBrush = NULL;
			}
			InvalidateRect(GetDlgItem(hWnd, IDOK), NULL, TRUE);
		}
		break;
	}

	case WM_CREATE:
	{
		// Select and create a temporary background brush
		hBgBrush = CreateSolidBrush(isDarkTheme ? kDarkBg : GetSysColor(COLOR_WINDOW));

		// Extract the CREATESTRUCT from lParam
		CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
		PDialogParams pParams = reinterpret_cast<PDialogParams>(cs->lpCreateParams);

		// Store parameters in GWLP_USERDATA for later use
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pParams));

		// Common control creation parameters
		const DWORD commonStyle = WS_CHILD | WS_VISIBLE;
		HWND hControl{};

		// Create controls with centralized error handling
		do {
			// Edit control
			hControl = CreateWindow(
				WC_EDIT, NULL,
				commonStyle | ES_MULTILINE | WS_VSCROLL | WS_BORDER,
				10, 10, 260, 115,
				hWnd, (HMENU)IDC_EDIT, GetModuleHandle(NULL), NULL
			);
			if (!hControl) { break; }

			// Set initial text
			if (pParams->szBuffer) { SetWindowText(hControl, pParams->szBuffer); }

			// Create a Tooltip
			g_hTooltip = CreateWindow(
				TOOLTIPS_CLASS, NULL,
				WS_POPUP | TTS_ALWAYSTIP,
				CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
				hControl, NULL, GetModuleHandle(NULL), NULL
			);

			if (g_hTooltip) {
				TOOLINFO ti = { sizeof(TOOLINFO) };
				ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS; // Use HWND, auto-handle mouse events
				ti.hwnd = hWnd;                          // Parent window
				ti.uId = (UINT_PTR)hControl;             // Handle of the edit control
				ti.lpszText = const_cast<LPWSTR>(_T(     // Tooltip text
					"Text format:\n"
					"One app name per line."
				));
				// Add the tooltip
				SendMessage(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
				// Enable multi-line by setting a max width(e.g., 200 pixels)
				SendMessage(g_hTooltip, TTM_SETMAXTIPWIDTH, 0, 200);
			}

			// OK Button
			hControl = CreateWindow(
				WC_BUTTON, _T("OK"),
				commonStyle | WS_TABSTOP | BS_PUSHBUTTON | BS_FLAT,
				115, 132, 70, 24,
				hWnd, (HMENU)IDOK, GetModuleHandle(NULL), NULL
			);
			if (!hControl) { break; }
			PostMessage(hControl, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

			// Cancel Button
			hControl = CreateWindow(
				WC_BUTTON, _T("Cancel"),
				commonStyle | WS_TABSTOP | BS_PUSHBUTTON | BS_FLAT,
				200, 132, 70, 24,
				hWnd, (HMENU)IDCANCEL, GetModuleHandle(NULL), NULL
			);
			if (!hControl) { break; }
			PostMessage(hControl, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

			// Pick theme
			SendMessage(hWnd, WM_APP_CUSTOM_MESSAGE,
				MAKEWPARAM(ID_THEME_CHANGED, 0),
				(LPARAM)NULL
			);

			// Refresh
			RedrawWindow(hWnd, NULL, NULL,
				RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);

			// Success - show window
			ShowWindow(hWnd, SW_SHOW);
			return 0;

		} while (false); // Fake loop for clean break

		// Error cleanup
		DestroyWindow(hWnd);
		g_hDialog = g_hTooltip = NULL;
		return -1;
	}

	default: break;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

// Static flag to ensure registration happens once
static BOOL RegisterDialogClass()
{
	static BOOL s_registered{};
	if (s_registered) { return TRUE; }

	WNDCLASSEX wc = { };
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = DialogProc; // Your dialog procedure
	wc.hInstance = GetModuleHandle(NULL);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = DIALOG_CLASSNAME;

	if (!RegisterClassEx(&wc)) { return FALSE; }
	return (s_registered = TRUE);
}

// Positions the window relative to the cursor while ensuring it stays within screen bounds
POINT PositionDialogAtCursor(SIZE siWindowSize)
{
	POINT ptCursor;
	GetCursorPos(&ptCursor); // Get current cursor position

	// Get the monitor nearest to the cursor
	HMONITOR hMonitor = MonitorFromPoint(ptCursor, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = { sizeof(MONITORINFO) };
	GetMonitorInfo(hMonitor, &mi);
	const RECT& rcWork = mi.rcWork; // Work area of the monitor

	// Calculate initial position (centered at cursor)
	POINT pt;
	pt.x = ptCursor.x - (siWindowSize.cx / 2);
	pt.y = ptCursor.y - (siWindowSize.cy / 2);

	// Adjust to ensure the dialog fits horizontally
	if (pt.x < rcWork.left) {
		pt.x = rcWork.left;
	}
	else if (pt.x + siWindowSize.cx > rcWork.right) {
		pt.x = rcWork.right - siWindowSize.cx;
	}
	// Adjust to ensure the dialog fits vertically
	if (pt.y < rcWork.top) {
		pt.y = rcWork.top;
	}
	else if (pt.y + siWindowSize.cy > rcWork.bottom) {
		pt.y = rcWork.bottom - siWindowSize.cy;
	}

	return pt;
}


// Creates a modeless edit dialog
HWND ShowCustomDialog(HWND hParent, PDialogParams pParams)
{
	if (!RegisterDialogClass()) { return NULL; }  // Ensure class exists

	const LONG lWindowWidth = 300;
	const LONG lWindowHeight = 200;

	// Check if dialog already exists
	if (g_hDialog && IsWindow(g_hDialog)) {
		SetForegroundWindow(g_hDialog);  // Bring existing dialog to foreground
		return g_hDialog;
	}

	// Copy the parameters to the heap
	PDialogParams pParamsCopy = new DialogParams{
		new TCHAR[pParams->cchMax], pParams->cchMax
	};
	_tcscpy_s(pParamsCopy->szBuffer, pParamsCopy->cchMax, pParams->szBuffer);


	// Calculate coordinates for positioning the dialog
	POINT pt = PositionDialogAtCursor({ lWindowWidth,lWindowHeight });

	return g_hDialog = CreateWindow(
		DIALOG_CLASSNAME, DIALOG_NAME,
		WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,  // Essential flags
		pt.x, pt.y, lWindowWidth, lWindowHeight,  // Position/size
		hParent, NULL, GetModuleHandle(NULL), pParamsCopy
	);
}



