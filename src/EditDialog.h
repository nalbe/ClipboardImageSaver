#pragma once

// Windows system headers
#include <windows.h>

// Standard library headers
#include <tchar.h>



// Structure to store buffer information and its size
typedef struct 
{
	LPTSTR szBuffer;  // Pointer to the text buffer
	DWORD cchMax;     // Size of the buffer (in characters)
} DialogParams, *PDialogParams;



extern LPCWSTR DIALOG_CLASSNAME;
extern LPCWSTR DIALOG_NAME;



// Function declaration to display a custom dialog
HWND ShowCustomDialog(HWND hParent, PDialogParams pParams);



