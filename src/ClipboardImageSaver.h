#pragma once
#include "resource.h"
#include <windows.h>
#include <tchar.h>
#include <utility>  // std::forward
#include <gdiplus.h>
#include <shellapi.h>  // Shell_NotifyIcon
#include "murmurhash3.h"  // Hash function
#include <uxtheme.h>  // Themes support
#include "IniSettings.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:wmainCRTStartup")



/*-----------------------------------------------------------------------------
 * MESSAGE SOURCES
 * Identifiers for different input sources in the application
 *----------------------------------------------------------------------------*/
#define MENU            (0)    // Command originated from a menu
#define ACCELERATOR     (1)    // Command originated from a keyboard accelerator
#define CONTROL         (2)    // Command originated from a control


/*-----------------------------------------------------------------------------
 * WINDOW MESSAGES
 * Custom window message IDs
 *----------------------------------------------------------------------------*/
#define WM_APP_TRAYICON                (WM_APP + 1)  // Custom tray icon notification message


/*-----------------------------------------------------------------------------
 * RESOURCE IDENTIFIERS
 * Icons, menu items, and control identifiers
 *----------------------------------------------------------------------------*/
#define IDI_NOTIFY_ICON                (WM_APP + 1)  // Application notification area icon


/*-----------------------------------------------------------------------------
 * TRAY MENU COMMANDS
 * Command identifiers for the notification area context menu
 *----------------------------------------------------------------------------*/
#define IDM_TRAY_SHOW_BALLOON          (WM_APP + 5)  // Show balloon notification
#define IDM_TRAY_RESTRICT_TO_SYSTEM    (WM_APP + 4)  // Restrict to system-only mode
#define IDM_TRAY_OPEN_FOLDER           (WM_APP + 3)  // Open output folder
#define IDM_TRAY_EXIT                  (WM_APP + 2)  // Exit application
#define IDM_TRAY_SEPARATOR             (WM_APP + 1)  // Menu separator item


/*-----------------------------------------------------------------------------
 * CONTROL MESSAGES
 * Custom control notification identifiers
 *----------------------------------------------------------------------------*/
#define IDC_APP_MULTIPLE_INSTANCES     (WM_APP + 1)  // Multiple instances control


// These enumerations represent possible outcomes of clipboard capture operations
enum class ClipboardResult
{
	Success,            // Data processed and saved
	NoData,             // No valid image data in clipboard
	ConversionFailed,   // CF_BITMAP to CF_DIB conversion failed
	LockFailed,         // Failed to lock clipboard data
	UnchangedContent,   // Same content as previous capture
	SaveFailed,         // File save operation failed
	InvalidParameter    // Invalid or malformed argument provided
};


// Contains all user-configurable settings that persist between application sessions
// The settings are loaded from and saved to an INI configuration file
extern struct AppSettings
{
	TCHAR iniPath[MAX_PATH];  // Full path to config file
	BOOL showNotifications;   // Enable/disable balloon notifications
	BOOL restrictToSystem;    // Restrict clipboard to svchost.exe
}g_settings;
