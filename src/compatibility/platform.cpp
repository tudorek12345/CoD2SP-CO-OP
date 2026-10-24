#include "compatibility/platform.hpp"
#include <bcrypt.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <regex>
namespace coop {
std::string sha256(const std::filesystem::path& path) {
 BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw std::runtime_error("SHA256 provider failed");
 DWORD size=0,result=0; BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&result,0);
 std::vector<unsigned char> object(size); unsigned char digest[32]{};
 if(BCryptCreateHash(alg,&hash,object.data(),size,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(alg,0);throw std::runtime_error("SHA256 initialization failed");}
 std::ifstream file(path,std::ios::binary); if(!file){BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);throw std::runtime_error("Cannot read executable");}
 std::vector<char> data(65536); bool ok=true;
 while(file.read(data.data(),data.size()) || file.gcount()) if(BCryptHashData(hash,reinterpret_cast<PUCHAR>(data.data()),static_cast<ULONG>(file.gcount()),0)<0)ok=false;
 ok=ok && file.eof() && BCryptFinishHash(hash,digest,32,0)>=0;
 BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg,0);
 if(!ok)throw std::runtime_error("SHA256 read/hash failure");
 std::ostringstream out;for(auto b:digest)out<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<unsigned>(b);return out.str();
}
std::filesystem::path module_path(HMODULE module){std::vector<wchar_t> path(32768);auto n=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));if(!n || n>=path.size())throw std::runtime_error("Module path failed");return std::wstring(path.data(),n);}
std::filesystem::path find_game(){
 const std::filesystem::path standard=L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Call of Duty 2";
 if(std::filesystem::exists(standard/L"CoD2SP_s.exe"))return standard;
 wchar_t steam[32768]{}; DWORD size=sizeof(steam);
 if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,nullptr,steam,&size)!=ERROR_SUCCESS)return {};
 std::filesystem::path base=steam; std::vector<std::filesystem::path> libraries{base};
 std::ifstream vdf(base/L"steamapps/libraryfolders.vdf");std::string text((std::istreambuf_iterator<char>(vdf)),{});
 std::regex pat("\"path\"\\s*\"([^\"]+)\"");
 for(std::sregex_iterator i(text.begin(),text.end(),pat),end;i!=end;++i)libraries.emplace_back(wide((*i)[1].str()));
 for(const auto& lib:libraries){auto manifest=lib/L"steamapps/appmanifest_2630.acf";std::ifstream f(manifest);std::string m((std::istreambuf_iterator<char>(f)),{});std::smatch match;std::regex install("\"installdir\"\\s*\"([^\"]+)\"");if(std::regex_search(m,match,install)){auto game=lib/L"steamapps/common"/wide(match[1].str());if(std::filesystem::exists(game/L"CoD2SP_s.exe"))return game;}}
 return {};
}
bool supported_sp(const std::filesystem::path& exe){return _wcsicmp(exe.filename().c_str(),L"CoD2SP_s.exe")==0 && sha256(exe)==sp_hash;}
std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),r.data(),n,nullptr,nullptr);return r;}
std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!n)throw std::runtime_error("Invalid UTF-8");std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),r.data(),n);return r;}
std::string json(const std::string& s){std::string r="\"";const char* h="0123456789abcdef";for(unsigned char c:s){if(c=='"'||c=='\\'){r+='\\';r+=c;}else if(c<32){r+="\\u00";r+=h[c>>4];r+=h[c&15];}else r+=c;}return r+'"';}
}
