#include "engine/player2_presentation.hpp"
#include "compatibility/platform.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

#if !defined(_MSC_VER) || !defined(_M_IX86)
#error Native presentation currently requires the validated MSVC x86 ABI.
#endif

namespace coop {
Player2Presentation* Player2Presentation::active_=nullptr;
namespace {
constexpr uintptr_t entities=0x1b15f00,stride=0x1fc;
using RenderScene=void(__cdecl*)(const void*);
using RegisterFont=void*(__cdecl*)(const char*,int);
using DrawTextInSpace=void(__cdecl*)(const char*,void*,const float*,const float*,const float*,const float*);
using TextWidth=int(__cdecl*)(const char*,int,void*);
using Intern=uint32_t(__cdecl*)(const char*,unsigned,unsigned,unsigned);
using Link=void(__cdecl*)(void*);
bool string_equals(uintptr_t address,const char* expected){
 const size_t length=strlen(expected)+1;char bytes[128]{};if(length>sizeof(bytes))return false;SIZE_T n=0;
 return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),bytes,length,&n)&&n==length&&!memcmp(bytes,expected,length);
}
RenderScene original_scene=nullptr;
RegisterFont register_font=nullptr;
DrawTextInSpace draw_text=nullptr;
TextWidth text_width=nullptr;
std::array<unsigned char,5> original_scene_bytes{};

template<class T>bool read(uintptr_t address,T& out){SIZE_T n=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&out,sizeof(out),&n)&&n==sizeof(out);}
template<class T>bool put(uintptr_t address,const T& value){SIZE_T n=0;return WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(value),&n)&&n==sizeof(value);}
bool matches(uintptr_t address,const char* hex){
 std::vector<unsigned char> bytes;for(size_t i=0;hex[i];i+=2){unsigned v=0;if(sscanf_s(hex+i,"%2x",&v)!=1)return false;bytes.push_back(static_cast<unsigned char>(v));}
 std::vector<unsigned char> actual(bytes.size());SIZE_T n=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),actual.data(),actual.size(),&n)&&n==actual.size()&&actual==bytes;
}
bool patch(uintptr_t address,const unsigned char* bytes){DWORD old=0;if(!VirtualProtect(reinterpret_cast<void*>(address),5,PAGE_EXECUTE_READWRITE,&old))return false;SIZE_T n=0;const bool ok=WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),bytes,5,&n)&&n==5;DWORD unused=0;const bool restored=VirtualProtect(reinterpret_cast<void*>(address),5,old,&unused)!=FALSE;FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),5);return ok&&restored;}
bool client_actor(uintptr_t entity){
 if(entity!=entities&&entity!=entities+stride)return false;unsigned char used=0;uint32_t client=0,sentient=0,base=0,capacity=0,server=0,entity_base=0;
 const uintptr_t index=(entity-entities)/stride;
 return read(0x1c60504,entity_base)&&entity_base==entities&&read(0x1c60500,base)&&base&&
  read(0x1c6052c,capacity)&&capacity>=1&&capacity<=2&&index<capacity&&read(0x825508,server)&&server&&
  read(entity+0xa4,used)&&used&&read(entity+0xfc,client)&&client==base+index*0x86b8&&read(entity+0x104,sentient)&&sentient;
}
__declspec(naked) unsigned model_index(const char*){
 __asm {
  push edi
  mov edi,[esp+8]
  mov eax,04d59f0h
  call eax
  pop edi
  ret
 }
}
__declspec(naked) void update_dobj(void*){
 __asm {
  mov eax,[esp+4]
  mov edx,04d6110h
  call edx
  ret
 }
}
__declspec(naked) int attach_model(void*,const char*,unsigned,unsigned){
 __asm {
  push ebx
  mov ebx,[esp+8]
  mov eax,[esp+20]
  push eax
  push dword ptr [esp+20]
  push dword ptr [esp+20]
  mov edx,04d62d0h
  call edx
  add esp,12
  pop ebx
  ret
 }
}
__declspec(naked) int bone_index(void*,unsigned){
 __asm {
  mov eax,[esp+4]
  push dword ptr [esp+8]
  mov edx,04725f0h
  call edx
  add esp,4
  ret
 }
}
__declspec(naked) void bone_world_matrix(void*,unsigned,float*){
 __asm {
  mov eax,[esp+8]
  push dword ptr [esp+12]
  push dword ptr [esp+8]
  mov edx,04d74b0h
  call edx
  add esp,8
  ret
 }
}
// The SP dispatcher deliberately skips player entity type 1. Its ordinary
// DObj submission helper accepts the same real centity without an AI actor.
__declspec(naked) void submit_client_body(void*){
 __asm {
  mov eax,[esp+4]
  mov edx,04aad00h
  call edx
  ret
 }
}
__declspec(naked) void use_animation_tree(void*,void*){
 __asm {
  lea eax,[esp+8]
  push dword ptr [esp+4]
  mov edx,04f7540h
  call edx
  add esp,4
  ret
 }
}
__declspec(naked) void goal_weight(void*,unsigned,float,float){
 __asm {
  push esi
  mov esi,[esp+8]
  push 0
  push 0
  push 03f800000h
  push dword ptr [esp+32]
  push dword ptr [esp+32]
  push dword ptr [esp+32]
  xor ecx,ecx
  mov edx,04795e0h
  call edx
  add esp,24
  pop esi
  ret
 }
}
__declspec(naked) int client_tag_origin(void*,void*,unsigned,float*){
 __asm {
  mov ecx,[esp+4]
  mov eax,[esp+12]
  push dword ptr [esp+16]
  push dword ptr [esp+12]
  mov edx,04abb20h
  call edx
  add esp,8
  ret
 }
}
std::string native_model_name(unsigned index){
 if(!index||index>255)return "none";uint16_t id=0;uint32_t strings=0;
 if(!read(0x81545a+index*2,id)||!id||!read(0x8aed00,strings)||!strings)return "unreadable";
 char name[128]{};if(!read(strings+4+id*8,name))return "unreadable";name[127]=0;
 return std::string(name);
}
}

void Player2Presentation::report(const std::string& text){{std::lock_guard<std::mutex> lock(mutex_);state_=text;}if(log_)log_(text);}
bool Player2Presentation::prepare_on_game_thread(Log logger){
 log_=std::move(logger);thread_=GetCurrentThreadId();
 if(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))!=0x400000||sha256(module_path())!=sp_hash){report("presentation rejected: unsupported executable");return false;}
 renderer_=GetModuleHandleW(L"gfx_d3d_x86_s.dll");
 if(!renderer_||sha256(module_path(renderer_))!="5b364adca51d945e3af2ec321be0646b38cb61ad129849939aca548d4eefa584")return false;
 if(!matches(0x4d59f0,"803f0056750433c05ec3be0100000090")||
    !matches(0x4d6110,"8b0d7810c60183ec6855568bf00fb746083bc157")||
    !matches(0x4d62d0,"51558b6c2410565733f68d9b000000008a8433d401000084c0740d46")||
    !matches(0x4725f0,"51530fb65818555633f63bde5789742410")||
    !matches(0x4d74b0,"83ec645355568b742474578bcee89effffff")||
    !matches(0x462cf0,"83ec0c8b4c2410538b5c241c5556578bd3")||
    !matches(0x4aad00,"83ec74568bf0f686b0000000200f8592000000")||
    !matches(0x4ad160,"565051ff15007b610033d283c40c3bc2")){report("presentation rejected: native model/bone/render signatures differ");return false;}
 const uintptr_t base=reinterpret_cast<uintptr_t>(renderer_);uintptr_t font=0,draw=0,width=0,scene=0;
 if(!read(base+0x748e00+62*4,font)||font!=base+0xbd40||!read(base+0x748e00+78*4,draw)||draw!=base+0x21850||
    !read(base+0x748e00+75*4,width)||width!=base+0xbfc0||!read(base+0x748e00+37*4,scene)||scene!=base+0x25bf0){report("presentation rejected: renderer exports differ");return false;}
 uint32_t font_global=0;unsigned char font_opcode=0;
 if(!matches(draw,"8b4424048038000f84d70000008d5001")||!read(font,font_opcode)||font_opcode!=0xa1||
    !read(font+1,font_global)||font_global!=base+0x748da8||!matches(font+5,"53555633ed85c0577e71")||
    !matches(width,"518b44240c555633ed33f6")){report("presentation rejected: text/font code differs");return false;}
 register_font=reinterpret_cast<RegisterFont>(font);draw_text=reinterpret_cast<DrawTextInSpace>(draw);text_width=reinterpret_cast<TextWidth>(width);
 animation_interfaces_=matches(0x4f7540,"5356578b7c2410578bd8e881ebffff8bb7f4010000")&&
  matches(0x4795e0,"d944240855d81d301e59008b6c2408dfe0")&&
  matches(0x42f280,"8b4304568d7480095756e8e1f90200");
 client_head_interface_=matches(0x4abb20,"83ec44535556578bf1508bc6e8bf6afcff8bf883c40485ff")&&
  matches(0x4ace50,"83ec108bc78bd6e8544df5ff85c07532");
 ready_=true;report("native model, DObj head and world text interfaces verified; no visual acceptance yet");return true;
}
bool Player2Presentation::assign_body_on_game_thread(uintptr_t entity,unsigned soldier){
 uint32_t initializing=0;if(!ready_||GetCurrentThreadId()!=thread_||!client_actor(entity)||!read(0x1c60528,initializing)||initializing!=1){report("body not assigned: requires a real player during native mission initialization");return false;}
 const char* body=soldier%2?"xmodel/playerbody_russian_padded02":"xmodel/playerbody_russian_padded01";
 const char* head=soldier%2?"xmodel/head_russian_winter_datien":"xmodel/head_russian_winter_alex";
 const char* helmet=soldier%2?"xmodel/helmet_russian_padded_b":"xmodel/helmet_russian_padded_a";
 const unsigned index=model_index(body);if(!index||index>255){report("body lookup failed");return false;}
 const auto model=static_cast<unsigned char>(index);if(!put(entity+0x114,model))return false;update_dobj(reinterpret_cast<void*>(entity));
 uint16_t empty_tag=0;if(!read(0x1c7f048,empty_tag))return false;
 if(!attach_model(reinterpret_cast<void*>(entity),head,empty_tag,1)||!attach_model(reinterpret_cast<void*>(entity),helmet,empty_tag,1)){report("body assigned but head/helmet attachment failed");return false;}
 reinterpret_cast<Link>(0x44b2d0)(reinterpret_cast<void*>(entity));
 head_tag_=static_cast<uint16_t>(reinterpret_cast<Intern>(0x462cf0)("j_head",0,7,6));
 uint16_t handle=0;if(!read(0x74a980+((entity-entities)/stride)*2,handle)||!handle){report("body model set but native DObj allocation was not observed");return false;}
 report(std::string("real player body/head/helmet assigned through native DObj; model=")+body+"; movement animation and pixels require gameplay verification");return true;
}
bool Player2Presentation::head_on_game_thread(uintptr_t entity,float output[3])const{
 if(!ready_||GetCurrentThreadId()!=thread_||!head_tag_||!client_actor(entity))return false;uint16_t handle=0;
 const uintptr_t index=(entity-entities)/stride,centity=0xf7eb80+index*0x184;
 if(client_head_interface_&&read(0x77f480+index*2,handle)&&handle&&handle<2048){
  const uintptr_t client_object=0x74c200+static_cast<uintptr_t>(handle)*100;
  if(client_tag_origin(reinterpret_cast<void*>(client_object),reinterpret_cast<void*>(centity),head_tag_,output))
   return std::isfinite(output[0])&&std::isfinite(output[1])&&std::isfinite(output[2]);
 }
 if(!read(0x74a980+((entity-entities)/stride)*2,handle)||!handle||handle>2048)return false;
 const uintptr_t object=0x74c200+static_cast<uintptr_t>(handle)*100;unsigned char models=0;if(!read(object+0x18,models)||!models||models>8)return false;
 const int bone=bone_index(reinterpret_cast<void*>(object),head_tag_);if(bone<0||bone>255)return false;float matrix[12]{};
 bone_world_matrix(reinterpret_cast<void*>(entity),static_cast<unsigned>(bone),matrix);
 for(unsigned i=0;i<3;++i){output[i]=matrix[9+i];if(!std::isfinite(output[i]))return false;}return true;
}
void Player2Presentation::set_name(const std::string& name){std::string clean;for(unsigned char c:name)if(c>=32&&c<127&&c!='^'&&clean.size()<24)clean.push_back(static_cast<char>(c));if(clean.empty())clean="Player 2";std::lock_guard<std::mutex> lock(mutex_);name_=clean;}
bool Player2Presentation::install_nametag_on_game_thread(uintptr_t entity,const std::string& name){
 if(!ready_||GetCurrentThreadId()!=thread_||installed_||(active_&&active_!=this))return false;actor_=entity;set_name(name);
 const uintptr_t scene=reinterpret_cast<uintptr_t>(renderer_)+0x25bf0;SIZE_T n=0;
 if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(scene),original_scene_bytes.data(),5,&n)||n!=5||original_scene_bytes[0]!=0xa0)return false;
 uint32_t address=0;memcpy(&address,original_scene_bytes.data()+1,4);if(address!=reinterpret_cast<uintptr_t>(renderer_)+0x1d4d00)return false;
 auto* trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,10,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!trampoline)return false;
 memcpy(trampoline,original_scene_bytes.data(),5);trampoline[5]=0xe9;const uint32_t back=static_cast<uint32_t>((scene+5)-(reinterpret_cast<uintptr_t>(trampoline)+10));memcpy(trampoline+6,&back,4);
 std::array<unsigned char,5> jump{};jump[0]=0xe9;const uint32_t displacement=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&render_scene_gate)-(scene+5));memcpy(jump.data()+1,&displacement,4);
 trampoline_=trampoline;original_scene=reinterpret_cast<RenderScene>(trampoline);active_=this;FlushInstructionCache(GetCurrentProcess(),trampoline,10);
 if(!patch(scene,jump.data())){patch(scene,original_scene_bytes.data());active_=nullptr;VirtualFree(trampoline,0,MEM_RELEASE);trampoline_=nullptr;return false;}
 installed_=true;report("native RenderScene nametag hook installed; text anchors to DObj j_head, pixel visibility awaits visual test");return true;
}
void __cdecl Player2Presentation::render_scene_gate(const void* refdef){if(active_)active_->render_body();original_scene(refdef);if(active_)active_->render_tag(refdef);}
void Player2Presentation::render_body(){
 if(GetCurrentThreadId()!=thread_||!client_actor(actor_))return;
 update_animation();
 const uintptr_t index=(actor_-entities)/stride,centity=0xf7eb80+index*0x184;
 uint16_t number=0,handle=0;unsigned char type=0,present=0;uint32_t flags=0,local=0;
 if(!read(centity+0xa0,type)||type!=1||!read(centity+0xa8,number)||number!=index||
    !read(centity+0x140,present)||!present||!read(centity+0xb0,flags)||(flags&0x20)||
    !read(0x77f480+index*2,handle)||!handle||handle>=2048)return;
 // Snapshot playerState.clientNum determines whose body the local camera hides.
 uint32_t snapshot=0;if(!read(0xf5bd28,snapshot)||!snapshot||!read(snapshot+0xb0,local)||local==index)return;
 submit_client_body(reinterpret_cast<void*>(centity));body_commands_.fetch_add(1);
}
void Player2Presentation::update_animation(){
 if(!animation_interfaces_||!client_actor(actor_))return;
 uint32_t current_tree=0;if(!read(actor_+0x1f4,current_tree))return;
 if(!animation_tree_){
  if(current_tree)return; // Preserve an engine or future module-owned tree.
  uintptr_t definition=0;
  for(unsigned slot=0;slot<33&&!definition;++slot){
   uint32_t tree=0,candidate=0,name=0,count=0;
   if(read(0x1c7e52c+slot*4,tree)&&tree&&read(tree,candidate)&&candidate&&
      read(candidate,name)&&string_equals(name,"generic_human")&&read(candidate+4,count)&&count>0&&count<=2048)definition=candidate;
  }
  uint32_t count=0,names=0;if(!definition||!read(definition+4,count)||!read(definition+8,names)||!names)return;
  unsigned idle=0,walk=0,run=0;
  for(unsigned i=1;i<count;++i){uint32_t name=0;uint16_t children=0;
   if(!read(names+i*4,name)||!name||!read(definition+12+i*8,children)||children)continue;
   if(string_equals(name,"standing_ambient_cold_baseposeidle1"))idle=i;
   if(string_equals(name,"unarmed_walk_russian"))walk=i;
   if(string_equals(name,"unarmed_run_russian"))run=i;
  }
  if(!idle||!walk||!run)return;
  // This exact internal helper allocates independent state from the native MT
  // pool and rebuilds this entity's DObj; it does not allocate an AI actor.
  use_animation_tree(reinterpret_cast<void*>(actor_),reinterpret_cast<void*>(definition));
  if(!read(actor_+0x1f4,current_tree)||!current_tree)return;
  uint32_t actual=0;if(!read(current_tree,actual)||actual!=definition)return;
  animation_tree_=current_tree;idle_anim_=idle;walk_anim_=walk;run_anim_=run;
  report("independent native generic_human animation tree assigned to real player; idle/walk/run pixels require gameplay verification");
 }
 if(current_tree!=animation_tree_)return;
 uint32_t client=0;if(!read(actor_+0xfc,client)||!client)return;float velocity[2]{};
 if(!read(client+0x20,velocity)||!std::isfinite(velocity[0])||!std::isfinite(velocity[1]))return;
 const float speed2=velocity[0]*velocity[0]+velocity[1]*velocity[1];
 const unsigned desired=speed2>10000.0f?run_anim_:(speed2>16.0f?walk_anim_:idle_anim_);
 if(last_anim_==desired)return;
 if(last_anim_)goal_weight(reinterpret_cast<void*>(animation_tree_),last_anim_,0.0f,0.15f);
 goal_weight(reinterpret_cast<void*>(animation_tree_),desired,1.0f,0.15f);last_anim_=desired;
}
void Player2Presentation::render_tag(const void* refdef){
 if(GetCurrentThreadId()!=thread_||!client_actor(actor_))return;std::array<float,12> view{};SIZE_T n=0;
 if(!ReadProcessMemory(GetCurrentProcess(),static_cast<const unsigned char*>(refdef)+0x18,view.data(),sizeof(view),&n)||n!=sizeof(view))return;
 for(float v:view)if(!std::isfinite(v))return;
 // refdef holds view origin followed by its three orthonormal view axes.
 for(unsigned a=0;a<3;++a){float length=0;for(unsigned i=0;i<3;++i)length+=view[3+a*3+i]*view[3+a*3+i];if(length<0.95f||length>1.05f)return;}
 std::array<int32_t,4> viewport{};std::array<float,2> fov{};
 if(read(reinterpret_cast<uintptr_t>(refdef),viewport)&&read(reinterpret_cast<uintptr_t>(refdef)+0x10,fov)){
  std::lock_guard<std::mutex> lock(mutex_);last_view_=view;last_viewport_=viewport;last_fov_=fov;last_view_time_=GetTickCount();
 }
 float head[3]{};if(!head_on_game_thread(actor_,head))return;head[2]+=10.0f;float right[3],down[3];for(unsigned i=0;i<3;++i){right[i]=-view[6+i]*0.22f;down[i]=-view[9+i]*0.22f;}
 std::string name;{std::lock_guard<std::mutex> lock(mutex_);name=name_;}auto* font=register_font("fonts/normalFont",1);if(!font)return;
 const int width=text_width(name.c_str(),24,font);if(width<=0||width>2048)return;for(unsigned i=0;i<3;++i)head[i]-=right[i]*(width*0.5f);
 const float color[4]={0.8f,0.94f,0.7f,1.0f};draw_text(name.c_str(),font,head,right,down,color);draw_commands_.fetch_add(1);
}
void Player2Presentation::uninstall_on_game_thread(){
 if(!installed_||GetCurrentThreadId()!=thread_)return;
 if(!patch(reinterpret_cast<uintptr_t>(renderer_)+0x25bf0,original_scene_bytes.data())){report("nametag hook removal failed; keep DLL resident");return;}
 installed_=false;active_=nullptr;VirtualFree(trampoline_,0,MEM_RELEASE);trampoline_=nullptr;report("native nametag hook removed");
}
std::string Player2Presentation::state_report()const{std::lock_guard<std::mutex> lock(mutex_);return state_+" native_body_submissions="+std::to_string(body_commands_.load())+" world_text_commands="+std::to_string(draw_commands_.load());}
std::string Player2Presentation::diagnostic_report_on_game_thread()const{
 if(GetCurrentThreadId()!=thread_)return "presentation diagnostic rejected: game thread required";
 std::ostringstream out;out<<"presentation ready="<<ready_<<" hook="<<installed_<<" actor_backing_valid="<<client_actor(actor_)<<" body_submissions="<<body_commands_.load()<<" tag_commands="<<draw_commands_.load();
 if(!client_actor(actor_))return out.str();
 const uintptr_t index=(actor_-entities)/stride,centity=0xf7eb80+index*0x184;
 float origin[3]{},head[3]{};unsigned char model=0,attachments[7]{},type=0,present=0;uint32_t flags=0;
 uint16_t game_handle=0,client_handle=0;read(actor_+0xdc,origin);read(actor_+0x114,model);read(actor_+0x1d4,attachments);
 read(centity+0xa0,type);read(centity+0x140,present);read(centity+0xb0,flags);read(0x74a980+index*2,game_handle);read(0x77f480+index*2,client_handle);
 out<<" entity="<<index<<" origin="<<origin[0]<<","<<origin[1]<<","<<origin[2]<<" model="<<static_cast<unsigned>(model)<<":"<<native_model_name(model);
 for(unsigned i=0;i<7;++i)if(attachments[i])out<<" attachment"<<i<<"="<<static_cast<unsigned>(attachments[i])<<":"<<native_model_name(attachments[i]);
 out<<" game_dobj="<<game_handle<<" client_dobj="<<client_handle<<" centity_type="<<static_cast<unsigned>(type)<<" present="<<static_cast<unsigned>(present)<<" flags="<<flags<<" no_draw="<<((flags&0x20)!=0);
 int head_bone=-1;if(client_handle&&client_handle<2048&&head_tag_)head_bone=bone_index(reinterpret_cast<void*>(0x74c200+client_handle*100),head_tag_);
 out<<" head_bone="<<head_bone<<" client_head_interface="<<client_head_interface_;
 const bool has_head=head_on_game_thread(actor_,head);out<<" head_valid="<<has_head;
 if(has_head)out<<" head="<<head[0]<<","<<head[1]<<","<<head[2];
 std::array<float,12> view{};std::array<int32_t,4> viewport{};std::array<float,2> fov{};DWORD view_time=0;
 {std::lock_guard<std::mutex> lock(mutex_);view=last_view_;viewport=last_viewport_;fov=last_fov_;view_time=last_view_time_;}
 out<<" camera_age_ms="<<(GetTickCount()-view_time)<<" camera="<<view[0]<<","<<view[1]<<","<<view[2]<<" fov="<<fov[0]<<","<<fov[1]<<" viewport="<<viewport[0]<<","<<viewport[1]<<","<<viewport[2]<<","<<viewport[3];
 out<<" axes=";for(unsigned i=3;i<12;++i)out<<view[i]<<(i==11?' ':'/');
 if(has_head&&view_time&&fov[0]>1&&fov[0]<179&&fov[1]>1&&fov[1]<179){
  float delta[3]={head[0]-view[0],head[1]-view[1],head[2]-view[2]},depth=0,left=0,up=0,distance2=0;
  for(unsigned i=0;i<3;++i){depth+=delta[i]*view[3+i];left+=delta[i]*view[6+i];up+=delta[i]*view[9+i];distance2+=delta[i]*delta[i];}
  out<<" distance="<<std::sqrt(distance2)<<" behind_camera="<<(depth<=0);
  if(depth>0.01f){const float x=-left/(depth*std::tan(fov[0]*0.008726646259971648f)),y=up/(depth*std::tan(fov[1]*0.008726646259971648f));
   out<<" ndc="<<x<<","<<y<<" in_frustum="<<(std::abs(x)<=1&&std::abs(y)<=1)<<" screen="<<(viewport[0]+viewport[2]*(1+x)*0.5f)<<","<<(viewport[1]+viewport[3]*(1-y)*0.5f);
  }
 }
 out<<" visible_pixels_unverified=1";return out.str();
}
}
