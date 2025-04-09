#pragma once


/*-----------------------------------------------------------------------------
 * WINDOW MESSAGES
 * Custom window message IDs
 *----------------------------------------------------------------------------*/
#define WM_APP_TRAYICON             (WM_APP + 1)  // Custom tray icon notification message
#define WM_APP_CUSTOM_MESSAGE       (WM_APP + 2)  // Custom message

 /*-----------------------------------------------------------------------------
  * RESOURCE IDENTIFIERS
  * Icons, menu items, and control identifiers
  *----------------------------------------------------------------------------*/
#define IDI_NOTIFY_ICON             (1000 + 1)  // Application notification area icon
#define IDC_EDIT                    (1000 + 2)  // Identifier for the edit control
#define IDT_DIALOG_ERROR_TIMER      (1000 + 3)  // Dialog timer
#define IDTT_DIALOG_TOOLTIP         (1000 + 4)  // Dialog tooltip

  /*-----------------------------------------------------------------------------
   * TRAY MENU COMMANDS
   * Command identifiers for the notification area context menu
   *----------------------------------------------------------------------------*/
#define IDM_TRAY_TOGGLE_NOTIFICATIONS  (WM_APP + 6)  // Command to show a balloon notification in the system tray
#define IDM_TRAY_TOGGLE_WHITELIST      (WM_APP + 5)  // Command to enable the whitelist filter
#define IDM_TRAY_EDIT_WHITELIST        (WM_APP + 4)  // Command to open the whitelist editor
#define IDM_TRAY_OPEN_FOLDER           (WM_APP + 3)  // Command to open the output folder
#define IDM_TRAY_EXIT                  (WM_APP + 2)  // Command to exit the application
#define IDM_TRAY_SEPARATOR             (WM_APP + 1)  // Separator item in the tray context menu

   /*-----------------------------------------------------------------------------
   * CUSTOM IDENTIFIERS
   * Custom command IDs (LOWORD)
   *----------------------------------------------------------------------------*/
#define ID_MULTIPLE_INSTANCES       (1000 + 1)  // Multiple instances control
#define ID_SYSTEM_THEME             (1000 + 2)  // System theme changes
#define ID_DIALOG_RESULT            (1000 + 3)  // Editing is finished



