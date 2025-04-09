#pragma once

// Windows system headers
#include <windows.h>        // Core Windows API definitions (e.g., HWND, WPARAM, SendMessage)

// Standard library headers
#include <unordered_set>    // Provides std::unordered_set for efficient unique element storage
#include <tchar.h>          // TCHAR support for Unicode/ANSI compatibility (e.g., _T macro)
#include <type_traits>      // Provides std::underlying_type_t for enum bitwise operations
#include <tuple>



// Define a custom end-of-line (EOL) sequence
#define EOL_ "\r\n"


// Forward declarations
class IniSettings;
class EditDialog;


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

enum class ThemeResult : unsigned
{
	None,
	DwmAttributeFailed   = 1 << 0,
	DwmAttributeDisabled = 1 << 1,
	ThemeFailed          = 1 << 2,
};


// Application settings structure
struct AppSettings
{
	static constexpr DWORD MAX_CONFIG_PATH_LENGTH = MAX_PATH;
	static constexpr DWORD MAX_WHITELIST_LENGTH   = MAX_PATH;

	TCHAR configFilePath[MAX_CONFIG_PATH_LENGTH];
	TCHAR processWhitelist[MAX_WHITELIST_LENGTH];
	BOOL configFileExists;
	BOOL notificationsEnabled;
	BOOL whitelistEnabled;
	std::unordered_set<UINT32> whitelistHashes;
};


// External declarations
namespace { extern AppSettings g_appSettings; }
extern UINT CF_PNG;




// ====================================
// Flag-style operations on enum types
// ====================================

// Bitwise OR
template <typename TEnum>
constexpr TEnum operator|(TEnum lhs_, TEnum rhs_) noexcept
{
	static_assert(std::is_enum_v<TEnum>, "Template parameter must be an enum type");
	return static_cast<TEnum>(
		static_cast<std::underlying_type_t<TEnum>>(lhs_) |
		static_cast<std::underlying_type_t<TEnum>>(rhs_)
		);
}

// Compound OR assignment
template <typename TEnum>
constexpr TEnum& operator|=(TEnum& lhs_, TEnum rhs_) noexcept
{
	static_assert(std::is_enum_v<TEnum>, "Template parameter must be an enum type");
	lhs_ = static_cast<TEnum>(
		static_cast<std::underlying_type_t<TEnum>>(lhs_) |
		static_cast<std::underlying_type_t<TEnum>>(rhs_)
		);
	return lhs_;
}

// Bitwise AND
template <typename TEnum>
constexpr TEnum operator&(TEnum lhs_, TEnum rhs_) noexcept
{
	static_assert(std::is_enum_v<TEnum>, "Template parameter must be an enum type");
	return static_cast<TEnum>(
		static_cast<std::underlying_type_t<TEnum>>(lhs_) &
		static_cast<std::underlying_type_t<TEnum>>(rhs_)
		);
}

// Compound AND assignment
template <typename TEnum>
constexpr TEnum& operator&=(TEnum& lhs_, TEnum rhs_) noexcept
{
	static_assert(std::is_enum_v<TEnum>, "Template parameter must be an enum type");
	lhs_ = static_cast<TEnum>(
		static_cast<std::underlying_type_t<TEnum>>(lhs_) &
		static_cast<std::underlying_type_t<TEnum>>(rhs_)
		);
	return lhs_;
}

// Flag check helper
template <typename TEnum>
constexpr bool HasFlag(TEnum value_, TEnum flag_) noexcept
{
	static_assert(std::is_enum_v<TEnum>, "Template parameter must be an enum type");
	return (static_cast<std::underlying_type_t<TEnum>>(value_) &
		static_cast<std::underlying_type_t<TEnum>>(flag_)) !=
		static_cast<std::underlying_type_t<TEnum>>(0);
}



// Helper struct to bundle format string and arguments
template<typename... Args>
struct Fmt
{
	LPCTSTR format;
	std::tuple<Args...> args;

	Fmt(LPCTSTR fmt, Args&&... a)
		: format(fmt), args(std::forward<Args>(a)...) {
	}
};

// Deduction guide for Fmt
template<typename... Args> Fmt(LPCTSTR, Args&&...) -> Fmt<Args...>;



