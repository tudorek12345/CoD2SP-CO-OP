#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
namespace coop {
inline constexpr const char* sp_hash = "ab17a89cc3722ae80edc1f4483cacbcae08b0373b9128fe4ec053ebc79062281";
inline constexpr const char* mp_hash = "41124c4620c1e96b8b908611a4135e3ccef4f675f56338d7f85bea6684afdf0e";
std::string sha256(const std::filesystem::path& path);
std::filesystem::path module_path(HMODULE module = nullptr);
std::filesystem::path find_game();
bool supported_sp(const std::filesystem::path& exe);
std::string utf8(const std::wstring& text);
std::wstring wide(const std::string& text);
std::string json(const std::string& text);
struct Handle { HANDLE value = nullptr; explicit Handle(HANDLE h = nullptr):value(h){} ~Handle(){if(value && value!=INVALID_HANDLE_VALUE) CloseHandle(value);} Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete; operator HANDLE()const{return value;} };
}
