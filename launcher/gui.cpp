#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <commctrl.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <iterator>

// Native UI for the diagnostic runtime. It never claims that an engine player
// bridge exists, and delegates hash checks and Steam launch to the CLI.
namespace {
constexpr int kHost=100, kJoin=101, kSoldier=102, kSettings=103, kBack=104;
constexpr int kStart=110, kStop=111, kLogs=112, kRefresh=113;
constexpr int kBind=120, kPort=121, kRemote=122, kAppearance=123;
enum class Page { Home, Host, Join, Soldier, Settings };
struct Binding { std::wstring address, label; };
struct App {
    HWND window{}, heading{}, description{}, bind_label{}, bind{}, port_label{}, port{};
    HWND remote_label{}, remote{}, soldier_label{}, soldier{}, model_note{};
    HWND start{}, stop{}, status{}, log{}, refresh{}, back{};
    HFONT font{}, title_font{}, small_font{};
    HBRUSH background{};
    HANDLE child{};
    Page page=Page::Home;
    std::vector<Binding> bindings;
    std::filesystem::path root, ini, launcher_log;
    std::wstring last_log, last_role;
    std::filesystem::file_time_type launched_at{};
    DWORD runtime_pid{};
    bool busy=false;
} app;
std::wstring get(HWND h) { int n=GetWindowTextLengthW(h); std::wstring s(static_cast<size_t>(n)+1,L'\0'); GetWindowTextW(h,s.data(),n+1); s.resize(static_cast<size_t>(n)); return s; }
void set(HWND h,const std::wstring& s) { SetWindowTextW(h,s.c_str()); }
std::wstring widen(const std::string& s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    UINT code=CP_UTF8;
    if(!n) { code=CP_ACP; n=MultiByteToWideChar(code,0,s.data(),static_cast<int>(s.size()),nullptr,0); }
    std::wstring r(n,L'\0'); if(n)MultiByteToWideChar(code,0,s.data(),static_cast<int>(s.size()),r.data(),n); return r;
}
std::wstring win_error(DWORD code) {
    wchar_t* s=nullptr; FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,reinterpret_cast<wchar_t*>(&s),0,nullptr);
    std::wstring result=s?s:L"Windows error"; if(s)LocalFree(s); return result+L" ("+std::to_wstring(code)+L")";
}
HWND control(const wchar_t* cls,const wchar_t* text,int x,int y,int w,int h,int id=0,DWORD style=0) {
    HWND c=CreateWindowExW((wcscmp(cls,L"EDIT")==0)?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,app.window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(app.font),TRUE); return c;
}
HWND label(const wchar_t* text,int x,int y,int w,int h) { return control(L"STATIC",text,x,y,w,h); }
std::wstring selected_bind() {
    LRESULT i=SendMessageW(app.bind,CB_GETCURSEL,0,0);
    return i>=0 && static_cast<size_t>(i)<app.bindings.size()?app.bindings[static_cast<size_t>(i)].address:L"127.0.0.1";
}
std::wstring setting(const wchar_t* name,const wchar_t* fallback) {
    wchar_t s[1024]{}; GetPrivateProfileStringW(L"launcher",name,fallback,s,1024,app.ini.c_str()); return s;
}
void save_settings() {
    auto bind=selected_bind(),port=get(app.port),remote=get(app.remote);
    auto soldier=std::to_wstring(std::max<LRESULT>(0,SendMessageW(app.soldier,CB_GETCURSEL,0,0)));
    WritePrivateProfileStringW(L"launcher",L"bind",bind.c_str(),app.ini.c_str());
    WritePrivateProfileStringW(L"launcher",L"port",port.c_str(),app.ini.c_str());
    WritePrivateProfileStringW(L"launcher",L"host",remote.c_str(),app.ini.c_str());
    WritePrivateProfileStringW(L"launcher",L"soldier",soldier.c_str(),app.ini.c_str());
}
void bindings() {
    auto previous=app.bindings.empty()?setting(L"bind",L"127.0.0.1"):selected_bind();
    app.bindings={{L"127.0.0.1",L"127.0.0.1  |  This computer (loopback)"}};
    ULONG bytes=16384; std::vector<unsigned char> buffer(bytes); ULONG result=ERROR_BUFFER_OVERFLOW;
    for(int attempt=0;attempt<3 && result==ERROR_BUFFER_OVERFLOW;++attempt) {
        buffer.resize(bytes);
        result=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&bytes);
    }
    if(result==NO_ERROR) {
        for(auto* a=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());a;a=a->Next) {
            if(a->OperStatus!=IfOperStatusUp || (a->IfType!=IF_TYPE_ETHERNET_CSMACD && a->IfType!=IF_TYPE_IEEE80211))continue;
            for(auto* u=a->FirstUnicastAddress;u;u=u->Next) {
                if(!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family!=AF_INET)continue;
                auto* v=reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr); wchar_t ip[INET_ADDRSTRLEN]{};
                if(!InetNtopW(AF_INET,&v->sin_addr,ip,INET_ADDRSTRLEN))continue;
                std::wstring address=ip; if(address==L"0.0.0.0" || address.rfind(L"127.",0)==0)continue;
                if(std::any_of(app.bindings.begin(),app.bindings.end(),[&](const Binding& b){return b.address==address;}))continue;
                std::wstring name=a->FriendlyName?a->FriendlyName:L"Network adapter";
                app.bindings.push_back({address,address+L"  |  "+name+(a->IfType==IF_TYPE_IEEE80211?L" (Wi-Fi)":L" (Ethernet)")});
            }
        }
    }
    SendMessageW(app.bind,CB_RESETCONTENT,0,0); int selection=0;
    for(size_t i=0;i<app.bindings.size();++i) {
        SendMessageW(app.bind,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(app.bindings[i].label.c_str()));
        if(app.bindings[i].address==previous)selection=static_cast<int>(i);
    }
    SendMessageW(app.bind,CB_SETCURSEL,selection,0);
    if(result!=NO_ERROR && result!=ERROR_NO_DATA)set(app.status,L"Adapter enumeration failed: "+win_error(result)+L". Loopback remains available.");
}
void show(HWND w,bool visible) { ShowWindow(w,visible?SW_SHOW:SW_HIDE); }
void page(Page p) {
    app.page=p; bool network=p==Page::Host || p==Page::Join || p==Page::Settings;
    show(app.bind_label,network); show(app.bind,network); show(app.refresh,network);
    show(app.port_label,network); show(app.port,network);
    show(app.remote_label,p==Page::Join); show(app.remote,p==Page::Join);
    show(app.soldier_label,p==Page::Soldier || p==Page::Host || p==Page::Join);
    show(app.soldier,p==Page::Soldier || p==Page::Host || p==Page::Join);
    show(app.model_note,p==Page::Soldier || p==Page::Host || p==Page::Join);
    show(app.start,p==Page::Host || p==Page::Join); show(app.back,p!=Page::Home);
    switch(p) {
    case Page::Home:
        set(app.heading,L"CO-OP CAMPAIGN");
        set(app.description,L"Host and Join open the native CO-OP menu in the original game.\r\nPlayer 2 and the client world mirror are under active development.");break;
    case Page::Host:
        set(app.heading,L"HOST CO-OP MISSION");
        set(app.description,L"Red Army Training  /  moscow  /  1 host + 1 client\r\nChoose the local Wi-Fi or Ethernet address your teammate can reach.");
        set(app.start,L"LAUNCH HOST SESSION");break;
    case Page::Join:
        set(app.heading,L"JOIN CO-OP MISSION");
        set(app.description,L"Enter the host's IPv4 address and matching port. The client stays at the game menu.\r\nCampaign mirroring is unavailable; connection status comes from actual runtime logs.");
        set(app.start,L"LAUNCH AND CONNECT");break;
    case Page::Soldier:
        set(app.heading,L"SOLDIER");
        set(app.description,L"Two Soviet infantry assets were found in the installed game.\r\nSelection is saved as a preference; no campaign actor is replaced.");break;
    case Page::Settings:
        set(app.heading,L"CONNECTION SETTINGS");
        set(app.description,L"Set the bind address and UDP port for the next session.\r\nLoopback is local testing. Choose Wi-Fi or Ethernet for LAN play.");break;
    }
    SetFocus(p==Page::Join?app.remote:p==Page::Soldier?app.soldier:p==Page::Home?GetDlgItem(app.window,kHost):app.bind);
}
std::wstring quote(const std::wstring& s) {
    // All values are either controlled paths or validated numeric/IP strings.
    std::wstring r=L"\""; size_t slashes=0;
    for(wchar_t c:s){if(c==L'\\'){++slashes;continue;}if(c==L'\"'){r.append(slashes*2+1,L'\\');r+=c;}else{r.append(slashes,L'\\');r+=c;}slashes=0;}
    r.append(slashes*2,L'\\'); return r+L"\"";
}
bool launch(const std::wstring& args) {
    if(app.child) {set(app.status,L"A launch is still in progress. Wait for its result.");return false;}
    auto cli=app.root/L"COD2CoopLauncher.exe";
    if(!std::filesystem::exists(cli)) {set(app.status,L"COD2CoopLauncher.exe is missing beside this control panel.");return false;}
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    HANDLE log=CreateFileW(app.launcher_log.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(log==INVALID_HANDLE_VALUE) {set(app.status,L"Cannot open launcher.log: "+win_error(GetLastError()));return false;}
    HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;si.hStdOutput=log;si.hStdError=log;si.hStdInput=input;
    PROCESS_INFORMATION pi{};std::wstring command=quote(cli.wstring())+L" "+args;
    BOOL ok=CreateProcessW(cli.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,app.root.c_str(),&si,&pi);
    DWORD error=GetLastError();CloseHandle(log);if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);
    if(!ok) {set(app.status,L"Launcher failed: "+win_error(error));return false;}
    CloseHandle(pi.hThread);app.child=pi.hProcess;app.busy=true;EnableWindow(app.start,FALSE);EnableWindow(app.stop,FALSE);
    set(app.status,L"Launching through the verified CLI. Waiting for Steam, runtime loading and real network status..."); return true;
}
void start() {
    std::wstring port=get(app.port),remote=get(app.remote); IN_ADDR parsed{};
    if(port.empty() || port.size()>5 || !std::all_of(port.begin(),port.end(),[](wchar_t c){return c>=L'0' && c<=L'9';})) {set(app.status,L"Port must be a number from 1024 to 65535.");SetFocus(app.port);return;}
    unsigned long n=wcstoul(port.c_str(),nullptr,10);if(n<1024 || n>65535){set(app.status,L"Port must be 1024..65535.");SetFocus(app.port);return;}
    if(app.page==Page::Join && (InetPtonW(AF_INET,remote.c_str(),&parsed)!=1 || parsed.S_un.S_addr==INADDR_ANY || parsed.S_un.S_addr==INADDR_BROADCAST)) {set(app.status,L"Enter a valid host IPv4 address, such as 192.168.1.20.");SetFocus(app.remote);return;}
    save_settings();app.launched_at=std::filesystem::file_time_type::clock::now();app.runtime_pid=0;
    auto soldier=std::to_wstring(std::max<LRESULT>(0,SendMessageW(app.soldier,CB_GETCURSEL,0,0)));
    std::wstring args=L"--launch --native-menu --attach-after-launch --bind "+selected_bind()+L" --port "+port+L" --soldier "+soldier+L" --logs "+quote((app.root/L"runtime").wstring());
    app.last_role=app.page==Page::Host?L"Host":L"Client";args+=app.page==Page::Host?L" --host":L" --join "+remote;
    launch(args);
}
std::wstring tail(const std::filesystem::path& p,size_t limit=16000) {
    std::ifstream f(p,std::ios::binary);if(!f)return {};
    f.seekg(0,std::ios::end);auto len=f.tellg();if(len<0)return {};
    if(len>static_cast<std::streamoff>(limit))f.seekg(-static_cast<std::streamoff>(limit),std::ios::end);else f.seekg(0);
    std::string bytes{std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
    std::wstring s=widen(bytes);std::wstring output;for(wchar_t c:s){if(c==L'\n' && (output.empty()||output.back()!=L'\r'))output+=L'\r';output+=c;}return output;
}
std::wstring runtime_status(const std::wstring& text) {
    size_t latest=std::wstring::npos;
    for(const wchar_t* event:{L"network",L"hook_failed",L"exception",L"shutdown"}) {
        auto p=text.rfind(std::wstring(L"\"event\":\"")+event+L"\"");
        if(p!=std::wstring::npos && (latest==std::wstring::npos || p>latest))latest=p;
    }
    if(latest==std::wstring::npos)return {};
    auto detail=text.find(L"\"detail\":\"",latest);if(detail==std::wstring::npos)return {};
    detail+=10;std::wstring result;bool escape=false;
    for(size_t i=detail;i<text.size() && result.size()<360;++i){wchar_t c=text[i];if(escape){result+=(c==L'n'||c==L'r')?L' ':c;escape=false;}else if(c==L'\\')escape=true;else if(c==L'\"')break;else result+=c;}
    return result;
}
void poll() {
    if(app.child && WaitForSingleObject(app.child,0)==WAIT_OBJECT_0) {
        DWORD exit=0;GetExitCodeProcess(app.child,&exit);CloseHandle(app.child);app.child=nullptr;app.busy=false;
        EnableWindow(app.start,TRUE);EnableWindow(app.stop,app.runtime_pid!=0);
        set(app.status,exit==0?L"Launcher completed. Runtime status appears below. Player bridge remains unavailable.":L"Launch failed (exit "+std::to_wstring(exit)+L"). Read launcher.log below for the actual error.");
    }
    auto text=tail(app.launcher_log,9000);std::filesystem::path runtime_log;
    std::error_code ec;auto runtime=app.root/L"runtime";std::filesystem::file_time_type newest{};
    for(std::filesystem::directory_iterator it(runtime,ec),end;!ec && it!=end;it.increment(ec)) {
        auto p=it->path()/L"runtime.jsonl";std::error_code status_ec;auto time=std::filesystem::last_write_time(p,status_ec);
        if(!status_ec && time>=app.launched_at && time>newest) {runtime_log=p;newest=time;}
    }
    if(!runtime_log.empty()) {
        auto pid=runtime_log.parent_path().filename().wstring();if(!pid.empty() && std::all_of(pid.begin(),pid.end(),[](wchar_t c){return c>=L'0' && c<=L'9';}))app.runtime_pid=wcstoul(pid.c_str(),nullptr,10);
        auto live=tail(runtime_log,11000);if(!app.busy){auto actual=runtime_status(live);if(!actual.empty())set(app.status,app.last_role+L" runtime: "+actual+L" | Player bridge unavailable.");}text+=L"\r\n--- Native runtime: "+runtime_log.wstring()+L" ---\r\n"+live;
        if(!app.busy && app.runtime_pid)EnableWindow(app.stop,TRUE);
    }
    if(text!=app.last_log) {app.last_log=text;set(app.log,text.empty()?L"No launch logs yet. Host or Join will start a real diagnostic session.":text);SendMessageW(app.log,EM_SETSEL,static_cast<WPARAM>(-1),static_cast<LPARAM>(-1));SendMessageW(app.log,EM_SCROLLCARET,0,0);}
}
void init(HWND window) {
    app.window=window; wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);app.root=std::filesystem::path(path).parent_path();app.ini=app.root/L"launcher.ini";app.launcher_log=app.root/L"launcher.log";app.launched_at=std::filesystem::file_time_type::clock::now();
    app.background=CreateSolidBrush(RGB(29,34,29));
    app.font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    app.title_font=CreateFontW(-32,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    app.small_font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto title=label(L"COD2 CO-OP",28,20,600,44);SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(app.title_font),TRUE);
    label(L"Cooperative Campaign Modification   |   ALPHA 0.1.0",30,67,800,26);
    auto warning=label(L"DIAGNOSTIC BUILD  |  SP player bridge unavailable: two-player campaign gameplay is not functional.",30,101,855,30);SendMessageW(warning,WM_SETFONT,reinterpret_cast<WPARAM>(app.small_font),TRUE);
    const wchar_t* nav[]={L"HOST GAME",L"JOIN GAME",L"SOLDIER",L"SETTINGS"};
    for(int i=0;i<4;++i)control(WC_BUTTONW,nav[i],30+i*178,141,162,36,kHost+i,WS_TABSTOP|BS_PUSHBUTTON);
    app.back=control(WC_BUTTONW,L"BACK",742,141,130,36,kBack,WS_TABSTOP|BS_PUSHBUTTON);
    app.heading=label(L"",30,199,840,30);app.description=label(L"",30,234,845,50);
    app.remote_label=label(L"Host IPv4 address",30,294,190,25);app.remote=control(L"EDIT",L"",226,290,310,29,kRemote,WS_TABSTOP|ES_AUTOHSCROLL);
    app.bind_label=label(L"Local bind address",30,333,190,25);app.bind=control(WC_COMBOBOXW,L"",226,329,510,220,kBind,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);app.refresh=control(WC_BUTTONW,L"REFRESH",750,329,122,30,kRefresh,WS_TABSTOP);
    app.port_label=label(L"UDP port",30,372,190,25);app.port=control(L"EDIT",L"28961",226,368,120,29,kPort,WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL);SendMessageW(app.port,EM_SETLIMITTEXT,5,0);
    app.soldier_label=label(L"Soviet infantry variant",30,411,190,25);app.soldier=control(WC_COMBOBOXW,L"",226,407,510,150,kAppearance,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);
    SendMessageW(app.soldier,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Rifleman A  |  character_russian_padded_a"));SendMessageW(app.soldier,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Rifleman B  |  character_russian_padded_b"));
    app.model_note=label(L"Cosmetic preference only. Applying these models requires the missing engine player bridge.",226,440,640,25);SendMessageW(app.model_note,WM_SETFONT,reinterpret_cast<WPARAM>(app.small_font),TRUE);
    app.start=control(WC_BUTTONW,L"LAUNCH",30,477,316,39,kStart,WS_TABSTOP|BS_DEFPUSHBUTTON);
    app.stop=control(WC_BUTTONW,L"STOP RUNTIME",365,477,205,39,kStop,WS_TABSTOP);EnableWindow(app.stop,FALSE);
    control(WC_BUTTONW,L"OPEN LOG FOLDER",588,477,284,39,kLogs,WS_TABSTOP);
    app.status=label(L"Ready. Select Host or Join to configure a session.",30,532,840,45);
    app.log=control(L"EDIT",L"",30,585,842,190,0,WS_TABSTOP|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|ES_AUTOHSCROLL|WS_VSCROLL|WS_HSCROLL);SendMessageW(app.log,EM_SETLIMITTEXT,100000,0);
    auto credit=label(L"Developed by Heperkoo  |  Unofficial modification. Requires a legitimate Call of Duty 2 installation.",30,790,850,25);SendMessageW(credit,WM_SETFONT,reinterpret_cast<WPARAM>(app.small_font),TRUE);
    set(app.port,setting(L"port",L"28961"));set(app.remote,setting(L"host",L"127.0.0.1"));
    auto selected=setting(L"soldier",L"0");SendMessageW(app.soldier,CB_SETCURSEL,selected==L"1"?1:0,0);bindings();page(Page::Home);SetTimer(window,1,750,nullptr);
}
LRESULT CALLBACK window_proc(HWND w,UINT m,WPARAM a,LPARAM b) {
    switch(m) {
    case WM_CREATE:init(w);return 0;
    case WM_COMMAND:
        switch(LOWORD(a)) {
        case kHost:page(Page::Host);break;case kJoin:page(Page::Join);break;case kSoldier:page(Page::Soldier);break;case kSettings:page(Page::Settings);break;
        case kBack:save_settings();page(Page::Home);break;case kRefresh:bindings();break;case kStart:start();break;
        case kStop:if(app.runtime_pid && launch(L"--stop "+std::to_wstring(app.runtime_pid)))set(app.status,L"Requesting native runtime shutdown...");break;
        case kLogs:ShellExecuteW(w,L"open",app.root.c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;
        }return 0;
    case WM_TIMER:try{poll();}catch(const std::exception& e){set(app.status,L"Log read failed: "+widen(e.what()));}return 0;
    case WM_CTLCOLORSTATIC:SetTextColor(reinterpret_cast<HDC>(a),RGB(227,230,216));SetBkColor(reinterpret_cast<HDC>(a),RGB(29,34,29));return reinterpret_cast<LRESULT>(app.background);
    case WM_ERASEBKGND:{RECT r{};GetClientRect(w,&r);FillRect(reinterpret_cast<HDC>(a),&r,app.background);return 1;}
    case WM_CLOSE:save_settings();DestroyWindow(w);return 0;
    case WM_DESTROY:KillTimer(w,1);if(app.child)CloseHandle(app.child);DeleteObject(app.font);DeleteObject(app.title_font);DeleteObject(app.small_font);DeleteObject(app.background);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(w,m,a,b);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR command,int show) {
    WSADATA ws{};if(WSAStartup(MAKEWORD(2,2),&ws)!=0)return 1;
    SetProcessDPIAware();INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wc.lpszClassName=L"COD2CoopControl";RegisterClassW(&wc);
    RECT rect{0,0,904,832};AdjustWindowRectEx(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,0);
    HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"COD2 CO-OP — ALPHA 0.1.0 | Developed by Heperkoo",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
    if(!window){WSACleanup();return 1;}
    if(command && wcscmp(command,L"--smoke-test")==0) {
        bool ok=IsWindow(app.bind) && SendMessageW(app.bind,CB_GETCOUNT,0,0)>=1;
        page(Page::Host);set(app.port,L"0");start();ok=ok && !app.child && get(app.status).find(L"1024")!=std::wstring::npos;
        set(app.port,L"28961");page(Page::Join);set(app.remote,L"invalid-host");start();ok=ok && !app.child && get(app.status).find(L"IPv4")!=std::wstring::npos;
        page(Page::Settings);ok=ok && (GetWindowLongW(app.bind,GWL_STYLE)&WS_VISIBLE)!=0;
        page(Page::Soldier);ok=ok && SendMessageW(app.soldier,CB_GETCOUNT,0,0)==2;
        page(Page::Home);ok=ok && (GetWindowLongW(app.start,GWL_STYLE)&WS_VISIBLE)==0;
        {std::ofstream result(app.root/L"gui-smoke.json");result<<"{\"native_window_created\":true,\"bind_choices\":"<<app.bindings.size()<<",\"navigation_and_input_rejection\":"<<(ok?"true":"false")<<",\"game_launched\":false}\n";}
        DestroyWindow(window);WSACleanup();return ok?0:2;
    }
    ShowWindow(window,show);UpdateWindow(window);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){
        if(msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE){save_settings();page(Page::Home);continue;}
        if(msg.message==WM_KEYDOWN && msg.wParam==VK_RETURN){auto focus=GetFocus();if(focus==app.remote || focus==app.port || focus==app.bind || focus==app.soldier){if(app.page==Page::Host || app.page==Page::Join)start();else{save_settings();page(Page::Home);}continue;}}
        if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    WSACleanup();return static_cast<int>(msg.wParam);
}
