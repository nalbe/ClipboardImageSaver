#pragma once


// Structure to store buffer information and its size
typedef struct 
{
	LPTSTR szBuffer;  // Pointer to the text buffer
	DWORD cchMax;     // Size of the buffer (in characters)
} DialogParams, *PDialogParams;


// Define a callback function type for handling theme change events
typedef BOOL (CALLBACK *ThemeChangedCallback)(HWND hWnd, LPARAM lParam);


// Namespace for whitelist edit dialog
namespace WhitelistEditDialog
{
	inline constexpr LPCWSTR DIALOG_CLASSNAME = L"CISWhitelistEditClass";
	inline constexpr LPCWSTR DIALOG_NAME      = L"Whitelist Edit";
}


// Function declaration to display a custom dialog
HWND ShowCustomDialog(HWND hParent, PDialogParams pParams);



