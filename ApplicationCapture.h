#pragma once
#include "Capture.h"
#include <tlhelp32.h>
#include <vector>
#include <string>
namespace aster {
struct AppProcess { DWORD pid=0,parent=0;std::wstring exe; };
inline bool belongsToApp(const std::wstring&exe,int group){return group==0?_wcsicmp(exe.c_str(),L"chrome.exe")==0:(_wcsicmp(exe.c_str(),L"mpc-be64.exe")==0||_wcsicmp(exe.c_str(),L"mpc-be.exe")==0);}
inline std::vector<DWORD> applicationRoots(const std::vector<AppProcess>&all,int group){
 std::vector<DWORD>roots;
 for(const auto&p:all){if(!belongsToApp(p.exe,group))continue;DWORD parent=p.parent;bool covered=false;
  for(int depth=0;depth<64&&parent>0;++depth){auto it=std::find_if(all.begin(),all.end(),[&](const AppProcess&v){return v.pid==parent;});if(it==all.end()||it->pid==p.pid)break;if(belongsToApp(it->exe,group)){covered=true;break;}if(it->parent==parent)break;parent=it->parent;}
  if(!covered)roots.push_back(p.pid);
 }std::sort(roots.begin(),roots.end());roots.erase(std::unique(roots.begin(),roots.end()),roots.end());return roots;
}
inline std::vector<AppProcess> enumerateAppProcesses(){
 std::vector<AppProcess>all;HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snap==INVALID_HANDLE_VALUE)return all;PROCESSENTRY32W e{};e.dwSize=sizeof(e);if(Process32FirstW(snap,&e))do{all.push_back({e.th32ProcessID,e.th32ParentProcessID,e.szExeFile});}while(Process32NextW(snap,&e));CloseHandle(snap);return all;
}
// A process-loopback capture includes descendants. Assign each tree once across all inputs.
inline bool ancestorOf(const std::vector<AppProcess>&all,DWORD parent,DWORD child){
 for(int depth=0;depth<64&&child;++depth){if(child==parent)return true;auto it=std::find_if(all.begin(),all.end(),[&](const AppProcess&p){return p.pid==child;});if(it==all.end()||it->parent==child)break;child=it->parent;}return false;
}
inline bool sameExecutable(const std::wstring&a,const std::wstring&b){
 if(_wcsicmp(a.c_str(),b.c_str())==0)return true;
 auto mpc=[](const std::wstring&v){return _wcsicmp(v.c_str(),L"mpc-be.exe")==0||_wcsicmp(v.c_str(),L"mpc-be64.exe")==0;};return mpc(a)&&mpc(b);
}
inline std::vector<std::vector<DWORD>> selectedRoots(const std::vector<AppProcess>&all,const std::vector<std::wstring>&names,DWORD self){
 std::vector<std::vector<DWORD>> result(names.size());
 for(size_t i=0;i<names.size();++i)for(const auto&p:all){
  if(names[i].empty()||!sameExecutable(names[i],p.exe)||ancestorOf(all,p.pid,self))continue;
  bool covered=false;
  for(size_t j=0;j<names.size()&&!covered;++j)for(const auto&q:all){if(names[j].empty()||!sameExecutable(names[j],q.exe)||ancestorOf(all,q.pid,self))continue;
   if((q.pid!=p.pid&&ancestorOf(all,q.pid,p.pid))||(q.pid==p.pid&&j<i)){covered=true;break;}}
  if(!covered)result[i].push_back(p.pid);
 }
 for(auto&r:result){std::sort(r.begin(),r.end());r.erase(std::unique(r.begin(),r.end()),r.end());}return result;
}
// Slots are published once and never freed until the audio callback is detached.
// The callback does not lock, allocate, enumerate processes, or destroy captures.
class ApplicationCapture {
public:
 static constexpr int maxInstances=16,maxBlock=8192;
 struct Slot{std::unique_ptr<Capture>owned;std::atomic<Capture*>published{nullptr};std::atomic<bool>active{false};DWORD pid=0;};
 std::array<Slot,maxInstances>slots;
 std::atomic<int>state{0},instances{0},overflow{0};std::atomic<HRESULT>error{S_OK};
 std::atomic<uint64_t>frames{0},dropped{0},underruns{0};std::atomic<float>peak{0},peakL{0},peakR{0},peakHold{0};
 ~ApplicationCapture(){stop();}
 void refresh(const std::vector<DWORD>&roots,int rate){
  overflow=std::max(0,int(roots.size())-maxInstances);
  for(auto&s:slots)if(s.pid&&std::find(roots.begin(),roots.end(),s.pid)==roots.end()){s.active=false;s.owned->requestStop();}
  for(auto&s:slots)if(s.pid&&!s.active){s.owned->stop();s.pid=0;}
  for(auto pid:roots){auto it=std::find_if(slots.begin(),slots.end(),[&](const Slot&s){return s.pid==pid;});
   if(it==slots.end()){it=std::find_if(slots.begin(),slots.end(),[](const Slot&s){return s.pid==0;});if(it==slots.end())continue;
    if(!it->owned){it->owned=std::make_unique<Capture>();it->published.store(it->owned.get(),std::memory_order_release);}it->pid=pid;it->owned->start(pid,rate);it->active.store(true,std::memory_order_release);
   }else if(it->owned->state==0||it->owned->state==3)it->owned->start(pid,rate);
  }
  int count=0,ready=0;HRESULT err=S_OK;uint64_t f=0,d=0,u=0;
  for(auto&s:slots)if(s.pid&&s.active){++count;if(s.owned->state==2)++ready;if(FAILED(s.owned->error.load()))err=s.owned->error;f+=s.owned->frames.load();d+=s.owned->dropped.load();u+=s.owned->underruns.load();}
  instances=count;state=count==0?0:FAILED(err)?3:ready==count?2:1;error=err;frames=f;dropped=d;underruns=u;
 }
 void stop(){for(auto&s:slots){s.active=false;if(s.owned)s.owned->requestStop();}for(auto&s:slots){if(s.owned)s.owned->stop();s.pid=0;}state=0;instances=0;peak=0;peakL=0;peakR=0;}
 void discard(){for(auto&s:slots)if(s.owned)s.owned->discard();}
 void render(float*l,float*r,int n,bool audible,int rate){
  if(n>maxBlock)return;
  for(auto&s:slots){auto*c=s.published.load(std::memory_order_acquire);if(!c)continue;std::fill_n(tempL.data(),n,0.0f);std::fill_n(tempR.data(),n,0.0f);bool enabled=s.active.load(std::memory_order_acquire)&&audible;c->render(tempL.data(),tempR.data(),n,enabled,rate);if(enabled)for(int i=0;i<n;++i){l[i]+=tempL[size_t(i)];r[i]+=tempR[size_t(i)];}}
  float pl=0,pr=0;for(int i=0;i<n;++i){pl=std::max(pl,std::abs(l[i]));pr=std::max(pr,std::abs(r[i]));}peakL=pl;peakR=pr;peak=std::max(pl,pr);peakHold=std::max(peakHold.load(),peak.load());
 }
private:std::array<float,maxBlock>tempL{},tempR{};
};
}
