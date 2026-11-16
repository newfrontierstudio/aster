#pragma once
#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <wrl.h>
#include <array>
#include <atomic>
#include <thread>
#include <cmath>
#include <algorithm>
namespace aster {
using namespace Microsoft::WRL;
class Activation final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase, IActivateAudioInterfaceCompletionHandler> {
public:
 HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 HRESULT result=E_PENDING;
 ComPtr<IUnknown> object;
 AUDIOCLIENT_ACTIVATION_PARAMS params{};
 ~Activation(){CloseHandle(event);}
 HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
  auto hr=op->GetActivateResult(&result,&object);if(FAILED(hr))result=hr;
  SetEvent(event);return S_OK;
 }
};
struct Frame { float l=0,r=0; };
class Capture {
public:
 static constexpr uint64_t capacity=262144;
 std::atomic<HRESULT> error{S_OK};
 std::atomic<int> state{0}; // 0 stopped, 1 starting, 2 running, 3 failed
 std::atomic<uint64_t> frames{0}, dropped{0}, underruns{0};
 std::atomic<float> peak{0},peakHold{0};
 std::atomic<float> peakL{0},peakR{0};
 std::atomic<int> bufferMs{30};
 ~Capture(){stop();}
 void requestStop(){quit=true;}
 void discard(){read.store(write.load(std::memory_order_acquire),std::memory_order_release);phase=0;primed=false;}
 void start(DWORD pid,int sampleRate){
  stop(); rate=sampleRate;frames=0;dropped=0;underruns=0;peak=0;peakHold=0;error=S_OK;state=1;quit=false;
  worker=std::thread([this,pid,sampleRate]{run(pid,sampleRate);});
 }
 void stop(){quit=true;if(worker.joinable())worker.join();state=0;peak=0;peakL=0;peakR=0;}
 // Only the host audio thread touches read/phase/primed; producer never moves read.
 void render(float*left,float*right,int n,bool audible,int sampleRate){
  if(state.load()!=2 || !audible){read.store(write.load(std::memory_order_acquire),std::memory_order_release);primed=false;phase=0;return;}
  auto r=read.load(std::memory_order_relaxed), w=write.load(std::memory_order_acquire);
  auto requestedMs=bufferMs.load();if(requestedMs!=lastBufferMs){primed=false;phase=0;lastBufferMs=requestedMs;}
  const auto target=uint64_t(sampleRate*requestedMs/1000);
  if(!primed){if(w-r<target)return;if(w-r>target*2){r=w-target;read.store(r,std::memory_order_release);}primed=true;}
  if(w-r>uint64_t(sampleRate/4)){r=w-target;phase=0;read.store(r,std::memory_order_release);}
  const double step=1.0+std::clamp((double(w-r)-double(target))/double(sampleRate)*0.02,-0.002,0.002);
  for(int i=0;i<n;++i){
   if(w-r<2){underruns++;primed=false;phase=0;break;}
   const auto&a=buffer[r%capacity];const auto&b=buffer[(r+1)%capacity];
   left[i]=a.l+float(phase)*(b.l-a.l);right[i]=a.r+float(phase)*(b.r-a.r);
   phase+=step;auto advance=uint64_t(phase);phase-=double(advance);r+=advance;
  }
  read.store(r,std::memory_order_release);
 }
 int buffered()const{return int(write.load()-read.load());}
private:
 std::array<Frame,capacity> buffer{};
 std::atomic<uint64_t> read{0},write{0};
 std::atomic<bool> quit{false};std::thread worker;int rate=48000;
 double phase=0;bool primed=false;int lastBufferMs=30;
 void run(DWORD pid,int sampleRate){
  HANDLE target=OpenProcess(SYNCHRONIZE,FALSE,pid);if(!target){fail(HRESULT_FROM_WIN32(GetLastError()));return;}
  auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
  if(FAILED(com)){CloseHandle(target);fail(com);return;}
  { // Release all COM objects before CoUninitialize.
   auto h=Make<Activation>();h->params.ActivationType=AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
   h->params.ProcessLoopbackParams.TargetProcessId=pid;
   h->params.ProcessLoopbackParams.ProcessLoopbackMode=PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
   PROPVARIANT prop{};prop.vt=VT_BLOB;prop.blob.cbSize=sizeof(h->params);prop.blob.pBlobData=(BYTE*)&h->params;
   ComPtr<IActivateAudioInterfaceAsyncOperation> op;
   auto hr=ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,__uuidof(IAudioClient),&prop,h.Get(),&op);
   if(SUCCEEDED(hr)){if(WaitForSingleObject(h->event,5000)!=WAIT_OBJECT_0)hr=HRESULT_FROM_WIN32(ERROR_TIMEOUT);else hr=h->result;}
   ComPtr<IAudioClient> client;ComPtr<IAudioCaptureClient> cap;
   if(SUCCEEDED(hr))hr=h->object.As(&client);
   WAVEFORMATEX fmt{};fmt.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;fmt.nChannels=2;fmt.nSamplesPerSec=sampleRate;fmt.wBitsPerSample=32;fmt.nBlockAlign=8;fmt.nAvgBytesPerSec=sampleRate*8;
   if(SUCCEEDED(hr))hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK|AUDCLNT_STREAMFLAGS_EVENTCALLBACK,2000000,0,&fmt,nullptr);
   HANDLE audioEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
   if(SUCCEEDED(hr))hr=audioEvent?client->SetEventHandle(audioEvent):HRESULT_FROM_WIN32(GetLastError());
   if(SUCCEEDED(hr))hr=client->GetService(IID_PPV_ARGS(&cap));
   if(SUCCEEDED(hr))hr=client->Start();
   if(FAILED(hr))fail(hr);
   else {
    state=2;
    while(!quit){
     HANDLE events[]={target,audioEvent};auto signalled=WaitForMultipleObjects(2,events,FALSE,100);
     if(signalled==WAIT_OBJECT_0){hr=HRESULT_FROM_WIN32(ERROR_PROCESS_ABORTED);break;}
     if(signalled==WAIT_FAILED){hr=HRESULT_FROM_WIN32(GetLastError());break;}
     UINT32 available=0;hr=cap->GetNextPacketSize(&available);if(FAILED(hr))break;
     while(available && !quit){
      BYTE* data=nullptr;UINT32 n=0;DWORD flags=0;
      hr=cap->GetBuffer(&data,&n,&flags,nullptr,nullptr);if(FAILED(hr))break;
      const auto w=write.load(std::memory_order_relaxed),r=read.load(std::memory_order_acquire);
      const auto count=std::min(uint64_t(n),capacity-(w-r));float pk=0,pl=0,pr=0;auto*p=(float*)data;
      for(uint64_t i=0;i<count;++i){Frame f;if(!(flags&AUDCLNT_BUFFERFLAGS_SILENT)){f.l=p[i*2];f.r=p[i*2+1];if(!std::isfinite(f.l))f.l=0;if(!std::isfinite(f.r))f.r=0;}
       buffer[(w+i)%capacity]=f;pl=std::max(pl,std::abs(f.l));pr=std::max(pr,std::abs(f.r));pk=std::max(pl,pr);}
      write.store(w+count,std::memory_order_release);frames+=n;dropped+=n-count;peak=pk;peakL=pl;peakR=pr;peakHold=std::max(peakHold.load(),pk);
      cap->ReleaseBuffer(n);hr=cap->GetNextPacketSize(&available);if(FAILED(hr))break;
     }
     if(FAILED(hr))break;
    }
    client->Stop();if(FAILED(hr))fail(hr);
   }
   if(audioEvent)CloseHandle(audioEvent);
  }
  CoUninitialize();
  CloseHandle(target);
 }
 void fail(HRESULT hr){error=hr;state=3;}
};
}
