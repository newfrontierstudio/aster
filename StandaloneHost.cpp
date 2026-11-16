#include <JuceHeader.h>
#include "BundledPlugins.h"
#include "ApplicationCapture.h"
#include "Vst2Host.h"
#include "PeakHold.h"
#include <tlhelp32.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <deque>
#include <functional>
static juce::String jp(const char*s){return juce::String::fromUTF8(s);}
static bool isTestRun(){return juce::String(GetCommandLineW()).contains("--self-test")||juce::String(GetCommandLineW()).contains("--inputs-self-test");}
static juce::File home(){return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile(isTestRun()?"AsterHost/Tests":"AsterHost");}
static juce::String db(float v){return juce::String(juce::Decibels::gainToDecibels(v,-120.0f),1);}
class WindowControl final:public juce::Button {
 int type;
public:explicit WindowControl(int t):Button("Window control"),type(t){}
 void paintButton(juce::Graphics&g,bool over,bool down)override{
  g.fillAll(juce::Colour(over?(type==juce::DocumentWindow::closeButton?0xffa33b45:0xff293b48):0xff0f171f));
  g.setColour(juce::Colour(down?0xffffffff:0xffbccbd5));float x=getWidth()*0.5f,y=getHeight()*0.5f;juce::Path path;
  if(type==juce::DocumentWindow::closeButton){path.startNewSubPath(x-4,y-4);path.lineTo(x+4,y+4);path.startNewSubPath(x+4,y-4);path.lineTo(x-4,y+4);g.strokePath(path,juce::PathStrokeType(1.2f));}
  else if(type==juce::DocumentWindow::minimiseButton)g.fillRect(x-4,y+3,8.0f,1.2f);
  else {if(getToggleState()){g.drawRect(x-2,y-5,7.0f,7.0f,1.0f);g.setColour(juce::Colour(over?0xff293b48:0xff0f171f));g.fillRect(x-4,y-3,7.0f,7.0f);g.setColour(juce::Colour(0xffbccbd5));g.drawRect(x-4,y-3,7.0f,7.0f,1.0f);}else g.drawRect(x-4,y-4,8.0f,8.0f,1.1f);}
 }
};
class HostFont final:public juce::LookAndFeel_V4{
public:
 juce::Button* createDocumentWindowButton(int type)override{return new WindowControl(type);}
 HostFont(){setColour(juce::Slider::backgroundColourId,juce::Colour(0xff526575));setColour(juce::Slider::trackColourId,juce::Colour(0xff80d5c5));setColour(juce::Slider::thumbColourId,juce::Colour(0xffb2eadf));}
 void drawDocumentWindowTitleBar(juce::DocumentWindow&w,juce::Graphics&g,int width,int height,int titleX,int titleWidth,const juce::Image*,bool drawLeft)override{juce::ignoreUnused(width,drawLeft);g.fillAll(juce::Colour(0xff0f171f));g.setColour(juce::Colour(0xffdce0e3));g.setFont(juce::Font("Segoe UI",12,0));g.drawText(w.getName(),titleX+6,0,juce::jmax(0,titleWidth-12),height,juce::Justification::centredLeft,true);}
 void drawLinearSlider(juce::Graphics&g,int x,int y,int width,int height,float pos,float minPos,float maxPos,const juce::Slider::SliderStyle style,juce::Slider&slider)override{if(style!=juce::Slider::LinearHorizontal){juce::LookAndFeel_V4::drawLinearSlider(g,x,y,width,height,pos,minPos,maxPos,style,slider);return;}float cy=float(y)+float(height)*0.5f;g.setColour(slider.findColour(juce::Slider::backgroundColourId));g.fillRoundedRectangle(float(x),cy-1.5f,float(width),3,1.5f);g.setColour(slider.findColour(juce::Slider::trackColourId));g.fillRoundedRectangle(float(x),cy-1.5f,juce::jmax(0.0f,pos-float(x)),3,1.5f);g.setColour(slider.findColour(juce::Slider::thumbColourId));g.fillEllipse(pos-3.5f,cy-3.5f,7,7);}
 juce::Font getTextButtonFont(juce::TextButton&,int)override{return juce::Font("Segoe UI",12,0);}
 juce::Font getLabelFont(juce::Label&label)override{return juce::Font("Segoe UI",label.getComponentID()=="source-name"?15.0f:12.0f,0);}
 juce::Font getComboBoxFont(juce::ComboBox&)override{return juce::Font("Meiryo",12,0);}
 juce::Font getPopupMenuFont()override{return juce::Font("Meiryo",12,0);}
 void drawToggleButton(juce::Graphics&g,juce::ToggleButton&b,bool hi,bool down)override{auto bounds=b.getLocalBounds();if(b.getButtonText()=="Output mute"&&b.getToggleState()){g.setColour(juce::Colour(0xffa33b45));g.fillRoundedRectangle(bounds.toFloat(),4.0f);g.setColour(juce::Colour(0xffffd9dc));g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f),4.0f,1.0f);g.setColour(juce::Colours::white);g.setFont(juce::Font("Segoe UI",11,0));g.drawFittedText("OUTPUT MUTED",bounds.reduced(8,0),juce::Justification::centred,1);return;}drawTickBox(g,b,2,3,13,13,b.getToggleState(),b.isEnabled(),hi,down);g.setColour(juce::Colours::white);g.setFont(juce::Font("Segoe UI",11,0));g.drawFittedText(b.getButtonText(),bounds.withTrimmedLeft(21),juce::Justification::centredLeft,1);}
};
class Engine final:public juce::AudioIODeviceCallback{
public:
 struct Node{juce::String name,path;std::unique_ptr<juce::AudioPluginInstance>plugin;juce::AudioBuffer<float>work;std::atomic<bool>enabled{true};PeakHold meters[2];std::unique_ptr<juce::AudioPluginInstance>meterPlugin;juce::AudioBuffer<float>meterWork;float gain=1;};
 static constexpr float silenceDb=-90.1f;
 static float levelGain(float db){return db<=silenceDb+0.0001f?0.0f:juce::Decibels::decibelsToGain(db);}
 static constexpr int maxSources=16;
 int sourceCount=2;std::atomic<bool>present[maxSources]{},changing[maxSources]{};
 PeakHold sourceHolds[maxSources][2],chainHold,outputHolds[2];void resetPeakHolds(){for(auto&pair:sourceHolds)for(auto&h:pair)h.reset();chainHold.reset();for(auto&h:outputHolds)h.reset();}
 juce::AudioDeviceManager devices;std::vector<std::unique_ptr<Node>>nodes;juce::String preferredOutputDevice;
 std::unique_ptr<aster::ApplicationCapture>captures[maxSources];
 juce::String sourceNames[maxSources]={"Chrome / YouTube","MPC-BE"};std::atomic<int>meterMode{0};std::atomic<bool>miniVisible{true};
 juce::StringArray appGroups[maxSources];
 juce::String exes[maxSources]={"chrome.exe","mpc-be64.exe"};
 std::atomic<float>sourceLeft[maxSources]{{0},{0}},sourceRight[maxSources]{{0},{0}},outputLeft{0},outputRight{0};std::atomic<float>trims[maxSources]{{-6},{0}},masterDb{-8.722f},inputPeaks[maxSources]{{0},{0}},processedPeak{0},outputPeak{0},maxProcessed{0};
 std::atomic<bool>sourceMute[maxSources]{{false},{false}},outputMuted{false},running{false};
 std::atomic<juce::int64>blocks{0},badSamples{0};juce::uint32 lastScan=0;double sampleRate=48000;int blockSize=512;
 double appCpuPct=0;ULONGLONG lastCpuTime=0,lastCpuWall=0;bool testSignal=false;double testPhase=0;juce::String error;juce::var config;
 void sampleCpu(){FILETIME a,b,k,u;if(!GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u))return;ULARGE_INTEGER kernel,user;kernel.LowPart=k.dwLowDateTime;kernel.HighPart=k.dwHighDateTime;user.LowPart=u.dwLowDateTime;user.HighPart=u.dwHighDateTime;auto total=kernel.QuadPart+user.QuadPart;auto wall=GetTickCount64();if(lastCpuWall&&wall>lastCpuWall)appCpuPct=double(total-lastCpuTime)/10000.0/double(wall-lastCpuWall)*100.0/double(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));lastCpuTime=total;lastCpuWall=wall;}
 Engine(){home().createDirectory();for(int i=0;i<maxSources;++i){captures[i]=std::make_unique<aster::ApplicationCapture>();present[i]=i<2;changing[i]=false;if(i<2)appGroups[i].add(exes[i]);sourceLeft[i]=0;sourceRight[i]=0;inputPeaks[i]=0;if(i>=2){trims[i]=0;sourceMute[i]=false;}}}
 ~Engine(){stop();devices.removeAudioCallback(this);devices.closeAudioDevice();nodes.clear();}
 bool load(const juce::var&c){
  config=c;preferredOutputDevice=c.getProperty("outputDeviceName",juce::String()).toString();for(int i=0;i<2;++i)sourceNames[i]=c.getProperty(i==0?"chromeName":"mpcName",sourceNames[i]).toString();meterMode=juce::jlimit(0,2,int(c.getProperty("meterMode",0)));juce::VST3PluginFormat format;auto*list=c["nodes"].getArray();if(!list){error="No chain configuration";return false;}
  for(auto&item:*list)if(!loadNode(item,format))return false;
  trims[0]=float(c.getProperty("chromeTrimDb",-6));trims[1]=float(c.getProperty("mpcTrimDb",0));masterDb=float(c.getProperty("masterDb",-8.722));
  if(auto*inputs=c["inputs"].getArray()){for(auto&p:present)p=false;sourceCount=0;for(auto&input:*inputs){if(sourceCount==maxSources)break;int i=sourceCount++;present[i]=true;exes[i]=juce::File(input["exe"].toString()).getFileName();appGroups[i].clear();if(auto*apps=input["apps"].getArray()){for(auto&app:*apps){auto exe=juce::File(app.toString()).getFileName();if(exe.isNotEmpty()&&!appGroups[i].contains(exe,true))appGroups[i].add(exe);}}else if(exes[i].isNotEmpty())appGroups[i].add(exes[i]);exes[i]=appGroups[i].isEmpty()?juce::String():appGroups[i][0];sourceNames[i]=input.getProperty("name",exes[i]).toString();trims[i]=juce::jlimit(silenceDb,12.0f,float(input.getProperty("trimDb",0)));sourceMute[i]=bool(input.getProperty("muted",false));}}
  return true;
 }
 bool loadNode(const juce::var&item,juce::VST3PluginFormat&format){
  juce::File file(item["path"].toString());if(file.isDirectory()){auto files=file.findChildFiles(juce::File::findFiles,true,"*.vst3");if(files.isEmpty()){error="Missing VST3 binary: "+file.getFullPathName();return false;}file=files[0];}
  if(file.hasFileExtension("dll")){auto plugin=legacy::Instance::open(file,error);if(!plugin)return false;juce::MemoryBlock state;auto text=item["state"].toString();if(text.isNotEmpty()&&state.fromBase64Encoding(text))plugin->setStateInformation(state.getData(),int(state.getSize()));auto node=std::make_unique<Node>();node->name=item.getProperty("name",plugin->getName()).toString();node->path=file.getFullPathName();node->enabled=bool(item.getProperty("enabled",true));node->gain=float(item.getProperty("gain",1));node->plugin=std::move(plugin);prepareNode(*node);nodes.push_back(std::move(node));return true;}
  juce::OwnedArray<juce::PluginDescription>descriptions;format.findAllTypesForFile(descriptions,file.getFullPathName());
  juce::MemoryBlock preset,state;juce::File(item["preset"].toString()).loadFileAsData(preset);auto stateText=item["state"].toString();if(stateText.isNotEmpty())state.fromBase64Encoding(stateText);
  for(auto*d:descriptions){if(state.getSize()&&item.hasProperty("pluginUid")&&int(item["pluginUid"])!=d->uniqueId)continue;juce::String e;auto plugin=format.createInstanceFromDescription(*d,sampleRate,blockSize,e);if(!plugin)continue;
   bool restored=false;if(state.getSize()){plugin->setStateInformation(state.getData(),int(state.getSize()));restored=true;}else if(preset.getSize())restored=juce::VST3PluginFormat::setStateFromVSTPresetFile(plugin.get(),preset);
   if(!restored&&preset.getSize())continue;
   auto node=std::make_unique<Node>();node->name=item.getProperty("name",d->name).toString();node->path=file.getFullPathName();node->plugin=std::move(plugin);node->enabled=bool(item.getProperty("enabled",true));node->gain=float(item.getProperty("gain",1));prepareNode(*node);nodes.push_back(std::move(node));return true;
  }error="Could not load/restore "+file.getFileName();return false;
 }
 bool openDevice(){
  auto result=devices.initialise(0,2,nullptr,true);if(result.isNotEmpty()){error=result;return false;}
  auto&types=devices.getAvailableDeviceTypes();juce::String type;
  for(auto*t:types)if(t->getTypeName().containsIgnoreCase("Windows Audio")&&!t->getTypeName().containsIgnoreCase("exclusive")&&!t->getTypeName().containsIgnoreCase("low latency")){type=t->getTypeName();break;}
  if(type.isEmpty()){error="Shared Windows Audio backend missing";return false;}devices.setCurrentAudioDeviceType(type,true);
  auto*t=devices.getCurrentDeviceTypeObject();t->scanForDevices();auto names=t->getDeviceNames(false);auto defaultIndex=t->getDefaultDeviceIndex(false);juce::String defaultName=juce::isPositiveAndBelow(defaultIndex,names.size())?names[defaultIndex]:juce::String();
  if(preferredOutputDevice.isNotEmpty()&&!names.contains(preferredOutputDevice)){preferredOutputDevice={};error="Saved output is unavailable; using Windows default.";}
  auto target=preferredOutputDevice.isNotEmpty()?preferredOutputDevice:defaultName;if(target.isEmpty()&&!names.isEmpty())target=names[0];if(target.isEmpty()){error="No Windows audio output devices are available.";return false;}
  juce::AudioDeviceManager::AudioDeviceSetup setup;devices.getAudioDeviceSetup(setup);setup.outputDeviceName=target;setup.inputDeviceName={};setup.sampleRate=0;setup.bufferSize=0;setup.useDefaultInputChannels=false;setup.inputChannels.clear();setup.useDefaultOutputChannels=true;setup.outputChannels.clear();
  result=devices.setAudioDeviceSetup(setup,true);if(result.isNotEmpty()){error=result;return false;}
  devices.addAudioCallback(this);return true;
 }
 juce::StringArray availableOutputDevices(){auto*t=devices.getCurrentDeviceTypeObject();if(!t)return{};t->scanForDevices();return t->getDeviceNames(false);}
 bool selectOutputDevice(const juce::String&name){auto*t=devices.getCurrentDeviceTypeObject();if(!t){error="Windows audio output is unavailable.";return false;}t->scanForDevices();auto names=t->getDeviceNames(false);auto index=t->getDefaultDeviceIndex(false);juce::String target=name.isEmpty()?(juce::isPositiveAndBelow(index,names.size())?names[index]:juce::String()):name;if(target.isEmpty()||!names.contains(target)){error="That output device is no longer available.";return false;}juce::AudioDeviceManager::AudioDeviceSetup oldSetup;devices.getAudioDeviceSetup(oldSetup);auto setup=oldSetup;setup.outputDeviceName=target;setup.inputDeviceName={};setup.sampleRate=0;setup.bufferSize=0;setup.useDefaultInputChannels=false;setup.inputChannels.clear();setup.useDefaultOutputChannels=true;setup.outputChannels.clear();auto result=devices.setAudioDeviceSetup(setup,true);if(result.isNotEmpty()){devices.setAudioDeviceSetup(oldSetup,true);error="Could not open output: "+result;return false;}preferredOutputDevice=name;error.clear();return true;}
 void prepareNode(Node&n){n.plugin->setPlayConfigDetails(2,2,sampleRate,blockSize);n.plugin->prepareToPlay(sampleRate,blockSize);n.work.setSize(juce::jmax(2,n.plugin->getTotalNumInputChannels(),n.plugin->getTotalNumOutputChannels()),8192);}
 void start(bool audible){lastScan=0;running=true;outputMuted=!audible;refreshSources();}
 void stop(){running=false;outputMuted=true;for(auto&c:captures)c->stop();for(auto&p:inputPeaks)p=0;processedPeak=0;outputPeak=0;outputLeft=0;outputRight=0;for(int i=0;i<maxSources;++i){sourceLeft[i]=0;sourceRight[i]=0;}}
 int addSource(){for(int i=0;i<maxSources;++i)if(!present[i]){const juce::ScopedLock lock(devices.getAudioCallbackLock());exes[i]={};appGroups[i].clear();sourceNames[i]="Input "+juce::String(i+1);trims[i]=0;sourceMute[i]=false;sourceGains[i].setCurrentAndTargetValue(1);present[i]=true;sourceCount=juce::jmax(sourceCount,i+1);return i;}error="Up to 16 inputs can be added.";return -1;}
 bool sourceUses(int i,juce::String exe)const{for(auto&app:appGroups[i])if(aster::sameExecutable(app.toWideCharPointer(),exe.toWideCharPointer()))return true;return false;}
 bool selectSource(int i,juce::String exe){return selectSources(i,juce::StringArray{exe});}
 bool selectSources(int i,const juce::StringArray&requested){
  if(i<0||i>=maxSources||!present[i]){error="Input not available.";return false;}
  juce::StringArray selected;auto own=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFileName();
  for(auto&path:requested){auto exe=juce::File(path.trim()).getFileName();if(!exe.endsWithIgnoreCase(".exe")){error="Choose an application executable (.exe).";return false;}if(exe.equalsIgnoreCase(own)){error="aster cannot capture itself.";return false;}
   bool duplicate=false;for(auto&app:selected)if(aster::sameExecutable(app.toWideCharPointer(),exe.toWideCharPointer()))duplicate=true;if(!duplicate)selected.add(exe);
  }
  // Build the complete move before changing any live route. Cancel/invalid requests do nothing.
  juce::StringArray next[maxSources];std::vector<int>affected{i};
  for(int j=0;j<sourceCount;++j){next[j]=appGroups[j];if(j==i||!present[j])continue;bool moved=false;
   for(int n=next[j].size();--n>=0;)for(auto&app:selected)if(aster::sameExecutable(next[j][n].toWideCharPointer(),app.toWideCharPointer())){next[j].remove(n);moved=true;break;}
   if(moved)affected.push_back(j);
  }
  next[i]=selected;
  for(int j:affected)changing[j]=true;
  {const juce::ScopedLock barrier(devices.getAudioCallbackLock());}
  for(int j:affected){captures[j]->stop();captures[j]->discard();}
  for(int j:affected){bool autoName=sourceNames[j].startsWith("Input ")||sourceNames[j].startsWith("Apps")||sourceNames[j].equalsIgnoreCase(exes[j].upToLastOccurrenceOf(".",false,false));
   appGroups[j]=next[j];exes[j]=next[j].isEmpty()?juce::String():next[j][0];
   if(autoName||sourceNames[j].isEmpty()||(j==i&&next[j].size()==1))sourceNames[j]=next[j].isEmpty()?"Input "+juce::String(j+1):next[j].size()==1?exes[j].upToLastOccurrenceOf(".",false,false):"Apps ("+juce::String(next[j].size())+")";
   sourceHolds[j][0].reset();sourceHolds[j][1].reset();sourceLeft[j]=0;sourceRight[j]=0;
  }
  for(int j:affected)changing[j]=false;
  lastScan=0;error.clear();refreshSources();return true;
 }
 void removeSource(int i){if(i<0||i>=sourceCount)return;changing[i]=true;{const juce::ScopedLock barrier(devices.getAudioCallbackLock());present[i]=false;}captures[i]->stop();captures[i]->discard();exes[i]={};appGroups[i].clear();changing[i]=false;lastScan=0;}
 void refreshSources(){if(!running)return;auto now=juce::Time::getMillisecondCounter();if(now-lastScan<1000&&lastScan!=0)return;lastScan=now;auto all=aster::enumerateAppProcesses();std::vector<std::wstring>names;std::vector<int>owners;
  for(int i=0;i<sourceCount;++i)if(present[i])for(auto&app:appGroups[i]){names.emplace_back(app.toWideCharPointer());owners.push_back(i);}
  auto roots=aster::selectedRoots(all,names,GetCurrentProcessId());std::vector<std::vector<DWORD>>grouped(sourceCount);for(size_t n=0;n<roots.size();++n)grouped[owners[n]].insert(grouped[owners[n]].end(),roots[n].begin(),roots[n].end());
  for(int i=0;i<sourceCount;++i)if(present[i]&&!changing[i])captures[i]->refresh(grouped[i],int(sampleRate));
 }
 void audioDeviceAboutToStart(juce::AudioIODevice*d)override{sampleRate=d->getCurrentSampleRate();blockSize=d->getCurrentBufferSizeSamples();mix.setSize(2,8192);scratch.setSize(2,8192);dryMix.setSize(2,8192);midi.ensureSize(4096);for(int i=0;i<maxSources;++i){sourceGains[i].reset(sampleRate,0.03);sourceGains[i].setCurrentAndTargetValue(levelGain(trims[i].load()));}master.reset(sampleRate,0.03);master.setCurrentAndTargetValue(outputMuted?0.0f:levelGain(masterDb.load()));for(auto&n:nodes){prepareNode(*n);if(n->meterPlugin){n->meterPlugin->setPlayConfigDetails(2,2,sampleRate,blockSize);n->meterPlugin->prepareToPlay(sampleRate,blockSize);}}}
 void audioDeviceStopped()override{}
 void audioDeviceIOCallback(const float**,int,float**out,int channels,int count)override{
  for(int c=0;c<channels;++c)if(out[c])juce::FloatVectorOperations::clear(out[c],count);if(count>8192||!running)return;
  mix.clear(0,count);for(int s=0;s<maxSources;++s){if(!present[s]||changing[s])continue;scratch.clear(0,count);captures[s]->render(scratch.getWritePointer(0),scratch.getWritePointer(1),count,!sourceMute[s],int(sampleRate));sourceGains[s].setTargetValue(levelGain(trims[s].load()));float pl=0,pr=0;auto*l=mix.getWritePointer(0);auto*r=mix.getWritePointer(1);auto*sl=scratch.getReadPointer(0);auto*sr=scratch.getReadPointer(1);for(int i=0;i<count;++i){auto gain=sourceGains[s].getNextValue();float vl=sl[i]*gain,vr=sr[i]*gain;l[i]+=vl;r[i]+=vr;pl=juce::jmax(pl,std::abs(vl));pr=juce::jmax(pr,std::abs(vr));}sourceHolds[s][0].push(pl);sourceHolds[s][1].push(pr);sourceLeft[s]=pl;sourceRight[s]=pr;inputPeaks[s]=juce::jmax(pl,pr);}
  if(testSignal){for(int i=0;i<count;++i){float v=0.001f*float(std::sin(testPhase));testPhase+=6.283185307179586*997.0/sampleRate;if(testPhase>6.283185307179586)testPhase-=6.283185307179586;mix.addSample(0,i,v);mix.addSample(1,i,v);}}
  juce::ScopedNoDenormals noDenormals;
  const int mode=miniVisible.load(std::memory_order_relaxed)?meterMode.load(std::memory_order_relaxed):2;if(mode==1)for(int c=0;c<2;++c)dryMix.copyFrom(c,0,mix,c,0,count);
  midi.clear();
  for(auto&node:nodes){if(!node->enabled){node->meters[0].push(0);node->meters[1].push(0);continue;}node->work.clear(0,count);for(int c=0;c<2;++c)node->work.copyFrom(c,0,mix,c,0,count);juce::AudioBuffer<float>view(node->work.getArrayOfWritePointers(),node->work.getNumChannels(),count);node->plugin->processBlock(view,midi);midi.clear();for(int c=0;c<2;++c)mix.copyFrom(c,0,node->work,c,0,count);if(node->gain!=1.0f)mix.applyGain(0,count,node->gain);if(mode==0){node->meters[0].push(mix.getMagnitude(0,0,count));node->meters[1].push(mix.getMagnitude(1,0,count));}
   else if(mode==1&&node->meterPlugin){node->meterWork.clear(0,count);for(int c=0;c<2;++c)node->meterWork.copyFrom(c,0,dryMix,c,0,count);juce::AudioBuffer<float>isolated(node->meterWork.getArrayOfWritePointers(),node->meterWork.getNumChannels(),count);node->meterPlugin->processBlock(isolated,midi);midi.clear();node->meters[0].push(node->meterWork.getMagnitude(0,0,count)*std::abs(node->gain));node->meters[1].push(node->meterWork.getMagnitude(1,0,count)*std::abs(node->gain));}}
  auto pk=mix.getMagnitude(0,count);processedPeak=pk;chainHold.push(pk);maxProcessed=juce::jmax(maxProcessed.load(),pk);master.setTargetValue(outputMuted?0.0f:levelGain(masterDb.load()));float post=0,postL=0,postR=0,rawL=0,rawR=0;
  for(int i=0;i<count;++i){auto gain=master.getNextValue();for(int c=0;c<juce::jmin(2,channels);++c){float v=mix.getSample(c,i)*gain;if(!std::isfinite(v)){v=0;badSamples++;}if(c==0)rawL=juce::jmax(rawL,std::abs(v));else rawR=juce::jmax(rawR,std::abs(v));v=juce::jlimit(-1.0f,1.0f,v);if(out[c])out[c][i]=v;post=juce::jmax(post,std::abs(v));if(c==0)postL=juce::jmax(postL,std::abs(v));else postR=juce::jmax(postR,std::abs(v));}}outputHolds[0].push(rawL);outputHolds[1].push(rawR);outputLeft=postL;outputRight=postR;outputPeak=post;blocks++;
 }
 void moveTo(int from,int to){const juce::ScopedLock lock(devices.getAudioCallbackLock());if(from<0||to<0||from>=int(nodes.size())||to>=int(nodes.size())||from==to)return;auto begin=nodes.begin();if(from<to)std::rotate(begin+from,begin+from+1,begin+to+1);else std::rotate(begin+to,begin+from,begin+from+1);}void move(int index,int delta){moveTo(index,index+delta);}
 bool add(const juce::File&file){devices.removeAudioCallback(this);auto*o=new juce::DynamicObject();juce::var item(o);o->setProperty("path",file.getFullPathName());o->setProperty("name",file.getFileNameWithoutExtension());juce::VST3PluginFormat format;error.clear();bool ok=loadNode(item,format);devices.addAudioCallback(this);return ok;}
 std::unique_ptr<Node> take(int index){const juce::ScopedLock lock(devices.getAudioCallbackLock());if(index<0||index>=int(nodes.size()))return {};auto node=std::move(nodes[size_t(index)]);nodes.erase(nodes.begin()+index);return node;}
 void insert(int index,std::unique_ptr<Node> node){const juce::ScopedLock lock(devices.getAudioCallbackLock());nodes.insert(nodes.begin()+juce::jlimit(0,int(nodes.size()),index),std::move(node));}
 int indexOf(Node*node)const{for(size_t i=0;i<nodes.size();++i)if(nodes[i].get()==node)return int(i);return -1;}
 void remove(int index){auto node=take(index);if(node)node->plugin->releaseResources();}
 // Meter copies receive the dry, post-input-trim mix and never feed the audible chain.
 bool setMeterMode(int mode){
  mode=juce::jlimit(0,2,mode);std::vector<std::unique_ptr<juce::AudioPluginInstance>>copies;
  if(mode==1){juce::VST3PluginFormat format;for(auto&node:nodes){juce::String e;std::unique_ptr<juce::AudioPluginInstance>copy;
   if(juce::File(node->path).hasFileExtension("dll"))copy=legacy::Instance::open(juce::File(node->path),e);
   else {juce::PluginDescription d;node->plugin->fillInPluginDescription(d);d.fileOrIdentifier=node->path;copy=format.createInstanceFromDescription(d,sampleRate,blockSize,e);}
   if(!copy){error="Separate meter: "+node->name+" "+e;return false;}
   juce::MemoryBlock state;{const juce::ScopedLock lock(devices.getAudioCallbackLock());node->plugin->getStateInformation(state);}
   copy->setStateInformation(state.getData(),int(state.getSize()));copy->setPlayConfigDetails(2,2,sampleRate,blockSize);copy->prepareToPlay(sampleRate,blockSize);copies.push_back(std::move(copy));
  }}
  // Allocate off the audio thread, then publish under the callback lock.
  std::vector<juce::AudioBuffer<float>>buffers;for(auto&copy:copies)buffers.emplace_back(juce::jmax(2,copy->getTotalNumInputChannels(),copy->getTotalNumOutputChannels()),8192);
  std::vector<std::unique_ptr<juce::AudioPluginInstance>>old;old.reserve(nodes.size());
  {const juce::ScopedLock lock(devices.getAudioCallbackLock());for(size_t i=0;i<nodes.size();++i){old.push_back(std::move(nodes[i]->meterPlugin));if(mode==1){nodes[i]->meterPlugin=std::move(copies[i]);nodes[i]->meterWork=std::move(buffers[i]);}for(auto&m:nodes[i]->meters)m.reset();}meterMode=mode;}
  error.clear();return true;
 }
 void syncMeterCopies(){if(meterMode!=1)return;for(auto&node:nodes){if(!node->meterPlugin)continue;juce::MemoryBlock state;const juce::ScopedLock lock(devices.getAudioCallbackLock());node->plugin->getStateInformation(state);node->meterPlugin->setStateInformation(state.getData(),int(state.getSize()));}}
 juce::var snapshot(){auto*o=new juce::DynamicObject();juce::var v(o);o->setProperty("hostPid",int(GetCurrentProcessId()));o->setProperty("running",running.load());o->setProperty("muted",outputMuted.load());o->setProperty("sampleRate",sampleRate);o->setProperty("blockSize",blockSize);o->setProperty("nodes",int(nodes.size()));o->setProperty("chainHeldPeak",chainHold.get());o->setProperty("outputHeldPeakL",outputHolds[0].get());o->setProperty("outputHeldPeakR",outputHolds[1].get());o->setProperty("outputFullScale",outputHolds[0].get()>=1||outputHolds[1].get()>=1);o->setProperty("processedPeak",processedPeak.load());o->setProperty("maxProcessed",maxProcessed.load());o->setProperty("outputPeak",outputPeak.load());o->setProperty("badSamples",badSamples.load());o->setProperty("blocks",blocks.load());o->setProperty("masterDb",masterDb.load());o->setProperty("appCpuPercent",appCpuPct);o->setProperty("audioLoadPercent",devices.getCpuUsage()*100);o->setProperty("testSignal",testSignal);o->setProperty("meterMode",meterMode.load());o->setProperty("error",error);
  juce::Array<juce::var>sources;for(int i=0;i<sourceCount;++i){if(!present[i])continue;auto*s=new juce::DynamicObject();juce::var item(s);s->setProperty("exe",exes[i]);juce::Array<juce::var>apps;for(auto&app:appGroups[i])apps.add(app);s->setProperty("apps",apps);s->setProperty("name",sourceNames[i]);s->setProperty("instances",captures[i]->instances.load());s->setProperty("overflow",captures[i]->overflow.load());juce::Array<juce::var>roots;for(auto&slot:captures[i]->slots)if(slot.pid){auto*o=new juce::DynamicObject();juce::var entry(o);o->setProperty("pid",int(slot.pid));o->setProperty("state",slot.owned->state.load());o->setProperty("peakHold",slot.owned->peakHold.load());roots.add(entry);}s->setProperty("roots",roots);s->setProperty("trimDb",trims[i].load());s->setProperty("muted",sourceMute[i].load());s->setProperty("state",captures[i]->state.load());s->setProperty("error",int(captures[i]->error.load()));s->setProperty("frames",juce::int64(captures[i]->frames.load()));s->setProperty("peakHold",captures[i]->peakHold.load());s->setProperty("dropped",juce::int64(captures[i]->dropped.load()));s->setProperty("underruns",juce::int64(captures[i]->underruns.load()));sources.add(item);}o->setProperty("sources",sources);return v;
 }
 void save(const juce::String&window){auto*o=new juce::DynamicObject();juce::var v(o);juce::Array<juce::var>list;for(auto&node:nodes){juce::MemoryBlock state;{const juce::ScopedLock lock(devices.getAudioCallbackLock());node->plugin->getStateInformation(state);}auto*n=new juce::DynamicObject();juce::var item(n);n->setProperty("name",node->name);n->setProperty("path",node->path);n->setProperty("gain",node->gain);n->setProperty("enabled",node->enabled.load());juce::PluginDescription description;node->plugin->fillInPluginDescription(description);n->setProperty("pluginUid",description.uniqueId);n->setProperty("state",state.toBase64Encoding());list.add(item);}o->setProperty("nodes",list);juce::Array<juce::var>inputs;for(int i=0;i<sourceCount;++i)if(present[i]){auto*input=new juce::DynamicObject();juce::var entry(input);input->setProperty("exe",exes[i]);juce::Array<juce::var>apps;for(auto&app:appGroups[i])apps.add(app);input->setProperty("apps",apps);input->setProperty("name",sourceNames[i]);input->setProperty("trimDb",trims[i].load());input->setProperty("muted",sourceMute[i].load());inputs.add(entry);}o->setProperty("inputs",inputs);o->setProperty("chromeTrimDb",trims[0].load());o->setProperty("mpcTrimDb",trims[1].load());o->setProperty("masterDb",masterDb.load());o->setProperty("chromeName",sourceNames[0]);o->setProperty("mpcName",sourceNames[1]);o->setProperty("meterMode",meterMode.load());o->setProperty("outputDeviceName",preferredOutputDevice);o->setProperty("layoutRevision",11);o->setProperty("window",window);home().getChildFile("settings.json").replaceWithText(juce::JSON::toString(v));}
private:juce::MidiBuffer midi;juce::AudioBuffer<float>mix,scratch,dryMix;juce::SmoothedValue<float>sourceGains[maxSources],master;
};

class MeterFixture final:public juce::AudioPluginInstance {
public:float gain;int calls=0;explicit MeterFixture(float g):gain(g){}
 const juce::String getName()const override{return "Meter fixture";}
 void fillInPluginDescription(juce::PluginDescription&d)const override{d.name=getName();}
 void prepareToPlay(double,int)override{}void releaseResources()override{}
 void processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&)override{++calls;b.applyGain(gain);}
 bool hasEditor()const override{return false;}juce::AudioProcessorEditor*createEditor()override{return nullptr;}
 bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}double getTailLengthSeconds()const override{return 0;}
 int getNumPrograms()override{return 1;}int getCurrentProgram()override{return 0;}void setCurrentProgram(int)override{}const juce::String getProgramName(int)override{return {}; }void changeProgramName(int,const juce::String&)override{}
 void getStateInformation(juce::MemoryBlock&b)override{b.replaceAll(&gain,sizeof(gain));}void setStateInformation(const void*d,int n)override{if(n==sizeof(gain))std::memcpy(&gain,d,sizeof(gain));}
};
static bool verifySilenceFloor(juce::AudioIODevice*device){
 Engine e;e.testSignal=true;e.masterDb=-90;e.outputMuted=false;e.audioDeviceAboutToStart(device);e.running=true;
 juce::AudioBuffer<float>out(2,512);
 auto render=[&]{for(int n=0;n<10;++n)e.audioDeviceIOCallback(nullptr,0,out.getArrayOfWritePointers(),2,512);};
 render();bool finite=e.outputPeak>0&&out.getMagnitude(0,512)>0;
 e.masterDb=Engine::silenceDb;render();bool silent=e.outputPeak==0&&out.getMagnitude(0,512)==0;
 e.masterDb=0;render();return finite&&silent&&e.outputPeak>0&&e.badSamples==0;
}
static bool verifyMeterRouting(juce::AudioIODevice*device){
 Engine e;e.testSignal=true;e.masterDb=0;e.outputMuted=false;
 for(float gain:{2.0f,3.0f}){auto node=std::make_unique<Engine::Node>();node->plugin=std::make_unique<MeterFixture>(gain);node->meterPlugin=std::make_unique<MeterFixture>(gain);node->meterWork.setSize(2,8192);e.nodes.push_back(std::move(node));}
 e.audioDeviceAboutToStart(device);e.running=true;
 juce::AudioBuffer<float>chain(2,512),separate(2,512),off(2,512);
 auto render=[&](int mode,juce::AudioBuffer<float>&out){e.meterMode=mode;e.testPhase=0;for(auto&n:e.nodes)for(auto&m:n->meters)m.reset();e.audioDeviceIOCallback(nullptr,0,out.getArrayOfWritePointers(),2,512);};
 render(0,chain);float chainFirst=e.nodes[0]->meters[0].get(),chainSecond=e.nodes[1]->meters[0].get();
 render(1,separate);float separateFirst=e.nodes[0]->meters[0].get(),separateSecond=e.nodes[1]->meters[0].get();
 auto*c=static_cast<MeterFixture*>(e.nodes[0]->meterPlugin.get());int calls=c->calls;render(2,off);
 bool equal=true;for(int ch=0;ch<2;++ch)for(int i=0;i<512;++i)equal=equal&&chain.getSample(ch,i)==separate.getSample(ch,i)&&chain.getSample(ch,i)==off.getSample(ch,i);
 return equal&&std::abs(chainFirst-separateFirst)<1.e-8f&&std::abs(chainSecond-2*separateSecond)<1.e-8f&&c->calls==calls&&e.nodes[0]->meters[0].get()==0;
}
class PluginWindow final:public juce::DocumentWindow{
public:HostFont font;juce::AudioProcessor*processor;std::function<void()>onCommit;~PluginWindow()override{setLookAndFeel(nullptr);}PluginWindow(Engine::Node&node):DocumentWindow(node.name,juce::Colour(0xff171d26),DocumentWindow::closeButton),processor(node.plugin.get()){setLookAndFeel(&font);setUsingNativeTitleBar(false);setTitleBarHeight(26);if(auto*editor=node.plugin->createEditorIfNeeded())setContentOwned(editor,true);centreWithSize(getWidth(),getHeight());setVisible(true);}void closeButtonPressed()override{if(onCommit)onCommit();setVisible(false);}
};
#include "InputPicker.h"
#include "HostPanel.h"
#include "InputTests.h"
#include "BundledPluginTests.h"
class HostApp final:public juce::JUCEApplication,private juce::Timer{
public:const juce::String getApplicationName()override{return isTestRun()?"aster Test":"aster";}const juce::String getApplicationVersion()override{return "0.3.20";}bool moreThanOneInstanceAllowed()override{return false;}
 class Window final:public juce::DocumentWindow{public:HostApp&app;Window(HostApp&a,Panel*p):DocumentWindow("aster",juce::Colour(0xff171d26),allButtons),app(a){setLookAndFeel(&p->font);setUsingNativeTitleBar(false);setTitleBarHeight(26);setContentOwned(p,true);setWantsKeyboardFocus(true);setResizable(true,false);setResizeLimits(740,493,1100,1200);centreWithSize(740,583);juce::Image icon(juce::Image::ARGB,64,64,true);juce::Graphics g(icon);g.setColour(juce::Colour(0xff80d5c5));juce::Path star;star.addStar({32,32},4,12,29,0.4f);g.fillPath(star);setIcon(icon);}~Window()override{setLookAndFeel(nullptr);}bool keyPressed(const juce::KeyPress&key)override{if(auto*p=dynamic_cast<Panel*>(getContentComponent()))if(p->keyPressed(key))return true;return DocumentWindow::keyPressed(key);}void closeButtonPressed()override{app.engine->save(getWindowStateAsString());setVisible(false);}};
 class Tray final:public juce::SystemTrayIconComponent{public:HostApp&app;Tray(HostApp&a):app(a){juce::Image image(juce::Image::ARGB,64,64,true);juce::Graphics g(image);g.setColour(juce::Colour(0xff80d5c5));juce::Path star;star.addStar({32,32},4,12,29,0.4f);g.fillPath(star);setIconImage(image,juce::Image());setIconTooltip("aster");}void mouseDown(const juce::MouseEvent&e)override{if(e.mods.isPopupMenu()){juce::PopupMenu menu;menu.setLookAndFeel(&app.window->getContentComponent()->getLookAndFeel());menu.addItem(1,"Show aster");menu.addItem(2,"Mute output",true,app.engine->outputMuted);menu.addItem(3,"Quit");menu.showMenuAsync(juce::PopupMenu::Options(),[this](int id){if(id==1)show();if(id==2)app.engine->outputMuted=!app.engine->outputMuted;if(id==3)app.systemRequestedQuit();});}else show();}void show(){app.window->setVisible(true);app.window->toFront(true);}};
 std::unique_ptr<Engine>engine;std::unique_ptr<Window>window;std::unique_ptr<Tray>tray;bool rackPassed=true;bool testing=false;bool groupTesting=false;juce::int64 deadline=0;
 void initialise(const juce::String&command)override{if(command.contains("--self-test-bundled")){setApplicationReturnValue(verifyBundledPlugins()?0:5);quit();return;}if(command.contains("--inputs-self-test")){home().createDirectory();setApplicationReturnValue(verifyInputs()?0:4);quit();return;}testing=command.contains("--self-test");home().createDirectory();auto settings=juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("AsterHost/settings.json");auto config=juce::JSON::parse(settings);if(config.isVoid())config=juce::JSON::parse(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("Import/chain.json"));if(config.isVoid()&&!settings.existsAsFile()){auto*empty=new juce::DynamicObject();empty->setProperty("nodes",juce::Array<juce::var>());config=juce::var(empty);}engine=std::make_unique<Engine>();if(!engine->load(config)||!engine->openDevice()){home().getChildFile("startup-error.txt").replaceWithText(engine->error);setApplicationReturnValue(2);quit();return;}
  if(engine->meterMode==1&&!engine->setMeterMode(1)){engine->meterMode=0;}if(testing){rackPassed=verifySilenceFloor(engine->devices.getCurrentAudioDevice())&&verifyMeterRouting(engine->devices.getCurrentAudioDevice());PeakHold probe;probe.push(0.5f);probe.push(0);bool held=probe.get()==0.5f;probe.push(1.25f);probe.push(0);held=held&&probe.get()==1.25f;probe.reset();rackPassed=rackPassed&&held&&probe.get()==0;auto before=engine->nodes.size();auto path=before?engine->nodes.front()->path:juce::String();rackPassed=rackPassed&&before>0&&engine->add(juce::File(path))&&engine->nodes.size()==before+1;if(engine->nodes.size()>before)engine->remove(int(before));rackPassed=rackPassed&&engine->nodes.size()==before;if(before>1){auto*first=engine->nodes.front().get();engine->moveTo(0,int(before)-1);rackPassed=rackPassed&&engine->nodes.back().get()==first;engine->moveTo(int(before)-1,0);rackPassed=rackPassed&&engine->nodes.front().get()==first;}std::vector<aster::AppProcess>fixture{{1,0,L"chrome.exe"},{2,1,L"chrome.exe"},{3,0,L"chrome.exe"},{4,0,L"mpc-be64.exe"},{5,0,L"mpc-be.exe"}};rackPassed=rackPassed&&aster::applicationRoots(fixture,0)==std::vector<DWORD>{1,3}&&aster::applicationRoots(fixture,1)==std::vector<DWORD>{4,5};}auto*panel=new Panel(*engine);if(testing&&panel->rows.size()>1){int last=int(panel->rows.size())-1;auto*first=engine->nodes.front().get();juce::DragAndDropTarget::SourceDetails details("aster-effect:0",&panel->rows[0]->name,{10,10});rackPassed=rackPassed&&panel->rows[size_t(last)]->name.isInterestedInDragSource(details);panel->rows[size_t(last)]->name.itemDropped(details);rackPassed=rackPassed&&engine->nodes.back().get()==first;engine->moveTo(last,0);}if(testing){
 auto original=engine->sourceNames[0];panel->history.clear();for(int i=0;i<12;++i)panel->renameSource(0,"Test "+juce::String(i));rackPassed=rackPassed&&panel->history.size()==10;for(int i=0;i<10;++i)rackPassed=panel->history.undo()&&rackPassed;rackPassed=rackPassed&&engine->sourceNames[0]=="Test 1"&&!panel->history.undo();engine->sourceNames[0]=original;panel->sourceNames[0].setText(original,juce::dontSendNotification);
 if(!engine->nodes.empty()){auto*node=engine->nodes[0].get();auto old=node->name;panel->renameNode(node,"Renamed fixture");rackPassed=rackPassed&&node->name=="Renamed fixture";panel->history.undo();rackPassed=rackPassed&&node->name==old;auto n=engine->nodes.size();panel->rows[0]->remove.onClick();rackPassed=rackPassed&&engine->nodes.size()+1==n;panel->history.undo();rackPassed=rackPassed&&engine->nodes.size()==n&&engine->nodes[0].get()==node;}
 float old=engine->trims[0];panel->trims[0].onDragStart();panel->trims[0].setValue(-20,juce::sendNotificationSync);panel->trims[0].setValue(-22,juce::sendNotificationSync);panel->trims[0].onDragEnd();rackPassed=rackPassed&&panel->history.size()==1;panel->history.undo();rackPassed=rackPassed&&engine->trims[0]==old;panel->history.clear();panel->rebuild();
}window=std::make_unique<Window>(*this,panel);if(testing){float old=engine->trims[0];panel->trims[0].setValue(-15.5,juce::sendNotificationSync);rackPassed=window->keyPressed(juce::KeyPress('z',juce::ModifierKeys::ctrlModifier,0))&&rackPassed;rackPassed=rackPassed&&engine->trims[0]==old;panel->history.clear();}auto bounds=config["window"].toString();if(bounds.isNotEmpty()){window->restoreWindowStateFromString(bounds);if(int(config.getProperty("layoutRevision",0))<4)window->setSize(window->getWidth(),juce::jmax(540,int(std::round(window->getHeight()*0.85))));}if(int(config.getProperty("layoutRevision",0))<5)window->setSize(740,juce::jmax(620,window->getHeight()));if(bounds.isNotEmpty()&&int(config.getProperty("layoutRevision",0))<6)window->setSize(window->getWidth(),juce::jmax(548,window->getHeight()-12));if(bounds.isNotEmpty()&&int(config.getProperty("layoutRevision",0))<7)window->setSize(window->getWidth(),juce::jmax(543,window->getHeight()-5));if(bounds.isNotEmpty()&&int(config.getProperty("layoutRevision",0))<8)window->setSize(window->getWidth(),juce::jmax(538,window->getHeight()-5));if(bounds.isNotEmpty()&&int(config.getProperty("layoutRevision",0))<9)window->setSize(window->getWidth(),juce::jmax(523,window->getHeight()-15));if(bounds.isNotEmpty()&&int(config.getProperty("layoutRevision",0))<10)window->setSize(window->getWidth(),juce::jmax(493,window->getHeight()-30));panel->onSave=[this]{engine->save(window->getWindowStateAsString());};if(!testing){tray=std::make_unique<Tray>(*this);if(!command.contains("--background"))window->setVisible(true);}
  if(testing&&command.contains("--vst2-test")){engine->testSignal=true;rackPassed=rackPassed&&engine->add(juce::File("C:/Program Files/Steinberg/VstPlugins/TDR Kotelnikov.dll"));if(rackPassed){auto&plugin=*engine->nodes.back()->plugin;juce::MemoryBlock state;plugin.getStateInformation(state);plugin.setStateInformation(state.getData(),int(state.getSize()));auto*ed=plugin.createEditorIfNeeded();rackPassed=rackPassed&&state.getSize()>0&&ed!=nullptr;delete ed;}panel->rebuild();}if(testing&&command.contains("--separate-test")){rackPassed=engine->setMeterMode(1)&&rackPassed;panel->updateMeterSwitch();}if(testing&&command.contains("--off-test")){rackPassed=engine->setMeterMode(2)&&rackPassed;panel->updateMeterSwitch();}if(testing&&command.contains("--input-ui-test")){int i=engine->addSource();rackPassed=rackPassed&&i==2&&engine->selectSource(i,"aster-input-fixture.exe");engine->sourceMute[i]=true;panel->sourceNames[i].setText(engine->sourceNames[i],juce::dontSendNotification);panel->mute[i].setToggleState(true,juce::dontSendNotification);panel->resized();rackPassed=rackPassed&&panel->visibleInputs()==3;}if(testing&&command.contains("--group-capture-test")){groupTesting=true;auto apps=engine->appGroups[1];auto processes=aster::enumerateAppProcesses();for(auto name:{juce::String("mpc-be64.exe"),juce::String("iZotope Photon 2027.exe")}){bool used=engine->sourceUses(1,name);bool exists=std::any_of(processes.begin(),processes.end(),[&](const aster::AppProcess&p){return name.equalsIgnoreCase(juce::String(p.exe.c_str()));});if(!used&&exists){apps.add(name);break;}}rackPassed=engine->selectSources(1,apps)&&rackPassed;}if(testing||command.contains("--activate")){engine->start(!testing);if(testing){window->setVisible(true);InputPicker picker(engine->exes[0],[](juce::String){return false;},[](juce::String){},[]{});auto appCount=picker.getNumRows();picker.searchFor("impossible-aster-search-no-app-19");rackPassed=rackPassed&&picker.getNumRows()==0;picker.searchFor("");rackPassed=rackPassed&&picker.getNumRows()==appCount;{auto image=picker.createComponentSnapshot(picker.getLocalBounds());auto file=home().getChildFile("input-picker-preview.png").createOutputStream();file->setPosition(0);juce::PNGImageFormat().writeImageToStream(image,*file);file->truncate();}auto snapshot=panel->createComponentSnapshot(panel->getLocalBounds());auto stream=home().getChildFile("host-preview.png").createOutputStream();stream->setPosition(0);juce::PNGImageFormat().writeImageToStream(snapshot,*stream);stream->truncate();deadline=juce::Time::getMillisecondCounterHiRes()+10000;startTimer(200);}else engine->save(window->getWindowStateAsString());}
  if(!testing&&!engine->running)engine->start(true);
  }
 void shutdown()override{stopTimer();if(engine)engine->stop();tray.reset();window.reset();engine.reset();}
 void systemRequestedQuit()override{if(engine&&window&&!testing)engine->save(window->getWindowStateAsString());quit();}
 void anotherInstanceStarted(const juce::String&command)override{if(command.contains("--quit-for-update")){systemRequestedQuit();return;}if(window){window->setVisible(true);window->toFront(true);}}
 void timerCallback()override{if(juce::Time::getMillisecondCounterHiRes()<deadline)return;auto picture=window->createComponentSnapshot(window->getLocalBounds());auto stream=home().getChildFile("aster-window-preview.png").createOutputStream();stream->setPosition(0);juce::PNGImageFormat().writeImageToStream(picture,*stream);stream->truncate();auto s=engine->snapshot();s.getDynamicObject()->setProperty("rackAndRootsPassed",rackPassed);bool success=rackPassed&&engine->captures[1]->frames>0&&(engine->testSignal||engine->captures[1]->peakHold>0)&&engine->maxProcessed>0&&engine->outputPeak==0&&engine->badSamples==0;if(groupTesting){bool grouped=engine->appGroups[1].size()>=2&&engine->captures[1]->instances>=2&&engine->captures[1]->state==2;success=success&&grouped;s.getDynamicObject()->setProperty("groupedCapturePassed",grouped);}s.getDynamicObject()->setProperty("selfTestPassed",success);home().getChildFile("self-test.json").replaceWithText(juce::JSON::toString(s));setApplicationReturnValue(success?0:3);quit();}
};
START_JUCE_APPLICATION(HostApp)



















