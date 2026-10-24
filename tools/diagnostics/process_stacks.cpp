#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace {
struct Handle { HANDLE h=nullptr;explicit Handle(HANDLE v):h(v){}~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}operator HANDLE()const{return h;}Handle(const Handle&)=delete; };
struct Resume { HANDLE thread;bool active;~Resume(){if(active)ResumeThread(thread);} };
struct Module { uint64_t base,size;std::string name,path; };
std::string utf8(const std::wstring& text){if(text.empty())return{};int n=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),out.data(),n,nullptr,nullptr);return out;}
std::string json(const std::string& s){std::string out="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(c>=32)out+=c;else out+='?';}return out+'"';}
std::string hex(uint64_t address){std::ostringstream o;o<<"0x"<<std::hex<<address;return o.str();}
void frame(std::ostream& out,uint64_t address,const std::vector<Module>& modules,HANDLE process){
 out<<"{\"pc\":"<<json(hex(address));for(const auto& m:modules)if(address>=m.base&&address-m.base<m.size){out<<",\"module\":"<<json(m.name)<<",\"offset\":"<<json(hex(address-m.base));break;}
 std::vector<unsigned char> data(sizeof(SYMBOL_INFO)+1024);auto* symbol=reinterpret_cast<SYMBOL_INFO*>(data.data());symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=1023;DWORD64 displacement=0;if(SymFromAddr(process,address,&displacement,symbol))out<<",\"symbol\":"<<json(std::string(symbol->Name,symbol->NameLen))<<",\"symbol_offset\":"<<displacement;out<<"}";
}
}
int wmain(int argc,wchar_t** argv){
 if(argc!=3){std::cerr<<"usage: COD2CoopProcessStacks PID output.json\nCaptures x86 CoD2SP_s.exe only. Briefly suspends each thread while unwinding; resumes immediately. No memory patch, dump, authentication changes, or UI action.\n";return 2;}
 DWORD pid=0;try{pid=std::stoul(argv[1]);}catch(...){return 2;}
 Handle process(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid));if(!process.h){std::cerr<<"OpenProcess failed "<<GetLastError()<<"\n";return 1;}
 std::vector<wchar_t> path(32768);DWORD length=DWORD(path.size());if(!QueryFullProcessImageNameW(process,0,path.data(),&length)||_wcsicmp(std::filesystem::path(std::wstring(path.data(),length)).filename().c_str(),L"CoD2SP_s.exe")!=0){std::cerr<<"Refusing non-CoD2SP target\n";return 1;}
 BOOL wow=FALSE;SYSTEM_INFO system{};GetNativeSystemInfo(&system);if(!IsWow64Process(process,&wow)||(system.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64&&!wow)){std::cerr<<"Target is not supported x86\n";return 1;}
 std::ofstream out{std::filesystem::path(argv[2])};if(!out){std::cerr<<"Cannot create output\n";return 1;}
 std::vector<Module> modules;Handle module_snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid));MODULEENTRY32W m{};m.dwSize=sizeof(m);if(Module32FirstW(module_snapshot,&m))do{modules.push_back({reinterpret_cast<uintptr_t>(m.modBaseAddr),m.modBaseSize,utf8(m.szModule),utf8(m.szExePath)});}while(Module32NextW(module_snapshot,&m));
 SymSetOptions(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS);
 bool symbols=SymInitialize(process,".",TRUE)!=FALSE;
 out<<"{\"pid\":"<<pid<<",\"capture_tick_ms\":"<<GetTickCount64()<<",\"architecture\":\"x86\",\"symbol_handler\":"<<(symbols?"true":"false")<<",\"note\":\"Best-effort live stack sample; module offsets are observations, not recovered engine symbols. No original process memory patched. Each thread briefly suspended then resumed.\",\"modules\":[";
 bool comma=false;for(const auto& mod:modules){if(comma)out<<",";comma=true;out<<"{\"name\":"<<json(mod.name)<<",\"path\":"<<json(mod.path)<<",\"base\":"<<json(hex(mod.base))<<",\"size\":"<<mod.size<<"}";}out<<"],\"threads\":[";
 Handle threads(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0));THREADENTRY32 e{};e.dwSize=sizeof(e);comma=false;
 if(Thread32First(threads,&e))do{if(e.th32OwnerProcessID!=pid)continue;if(comma)out<<",";comma=true;out<<"{\"tid\":"<<e.th32ThreadID;
  Handle thread(OpenThread(THREAD_GET_CONTEXT|THREAD_SUSPEND_RESUME|THREAD_QUERY_INFORMATION,FALSE,e.th32ThreadID));if(!thread.h){out<<",\"error\":\"OpenThread\",\"code\":"<<GetLastError()<<"}";continue;}
  bool paused=SuspendThread(thread)!=DWORD(-1);Resume resume{thread,paused};if(!paused){out<<",\"error\":\"SuspendThread\",\"code\":"<<GetLastError()<<"}";continue;}
  CONTEXT context{};context.ContextFlags=CONTEXT_FULL;if(!GetThreadContext(thread,&context)){out<<",\"error\":\"GetThreadContext\",\"code\":"<<GetLastError()<<"}";continue;}
  out<<",\"eip\":"<<json(hex(context.Eip))<<",\"esp\":"<<json(hex(context.Esp))<<",\"ebp\":"<<json(hex(context.Ebp))<<",\"frames\":[";frame(out,context.Eip,modules,process);
  STACKFRAME64 walk{};walk.AddrPC={context.Eip,0,AddrModeFlat};walk.AddrStack={context.Esp,0,AddrModeFlat};walk.AddrFrame={context.Ebp,0,AddrModeFlat};uint64_t prior=context.Eip;uint64_t began=GetTickCount64();
  for(unsigned i=0;i<63&&GetTickCount64()-began<250;++i){if(!StackWalk64(IMAGE_FILE_MACHINE_I386,process,thread,&walk,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr)||!walk.AddrPC.Offset||walk.AddrPC.Offset==prior)break;prior=walk.AddrPC.Offset;out<<",";frame(out,prior,modules,process);}out<<"]}";
 }while(Thread32Next(threads,&e));out<<"]}\n";if(symbols)SymCleanup(process);std::cout<<"Captured observed module/thread addresses for PID "<<pid<<"\n";return 0;
}
