#pragma once
// Minimal Windows x64 VST2 effect host. ABI declarations, no legacy SDK required.
namespace legacy {
struct Effect;
using Call=intptr_t (__cdecl*)(Effect*,int,int,intptr_t,void*,float);
struct Effect {
 int magic;Call dispatch;void(__cdecl*process)(Effect*,float**,float**,int);
 void(__cdecl*set)(Effect*,int,float);float(__cdecl*get)(Effect*,int);
 int programs,parameters,inputs,outputs,flags;intptr_t reserved1,reserved2;
 int latency,quality1,quality2;float ratio;void*object,*user;int id,version;
 void(__cdecl*replace)(Effect*,float**,float**,int);void*doubleReplace;char future[56];
};
struct TimeInfo{double position=0,rate=96000,nanos=0,ppq=0,tempo=120,bar=0,cycleStart=0,cycleEnd=0;int numerator=4,denominator=4,smpteOffset=0,smpteRate=0,clock=0,flags=0;};
class Instance final:public juce::AudioPluginInstance {
 HMODULE module=nullptr;Effect*fx=nullptr;juce::File file;bool prepared=false;double rate=96000;int block=960;TimeInfo time;
 juce::AudioBuffer<float>inputs,outputs;
 static intptr_t __cdecl host(Effect*e,int op,int index,intptr_t value,void*ptr,float){auto*self=e?static_cast<Instance*>(e->user):nullptr;switch(op){case 0:if(self&&index>=0&&index<self->getParameters().size())self->getParameters()[index]->sendValueChangedMessageToListeners(self->fx->get(self->fx,index));return 0;case 1:return 2400;case 3:return 0;case 7:return self?reinterpret_cast<intptr_t>(&self->time):0;case 13:if(self)self->setLatencySamples(self->fx->latency);return 1;case 15:if(self&&self->getActiveEditor()){self->getActiveEditor()->setSize(index,int(value));return 1;}return 0;case 16:return self?intptr_t(self->rate):96000;case 17:return self?self->block:960;case 23:return 2;case 32:if(ptr)strcpy_s(static_cast<char*>(ptr),64,"Aster Audio");return 1;case 33:if(ptr)strcpy_s(static_cast<char*>(ptr),64,"Aster Host");return 1;case 34:return 400;case 37:return ptr&&(strcmp(static_cast<char*>(ptr),"sizeWindow")==0||strcmp(static_cast<char*>(ptr),"sendVstTimeInfo")==0)?1:0;case 38:return 1;default:return 0;}}
 struct Parameter final:public juce::HostedAudioProcessorParameter {
  Instance&owner;int index;Parameter(Instance&o,int i):owner(o),index(i){}
  juce::String getParameterID()const override{return juce::String(index);}float getValue()const override{return owner.fx->get(owner.fx,index);}void setValue(float v)override{owner.fx->set(owner.fx,index,v);}float getDefaultValue()const override{return 0;}
  juce::String getName(int max)const override{char text[256]{};owner.send(8,index,0,text);return juce::String::fromUTF8(text).substring(0,max);}juce::String getLabel()const override{char text[256]{};owner.send(6,index,0,text);return juce::String::fromUTF8(text);}juce::String getText(float,int max)const override{char text[256]{};owner.send(7,index,0,text);return juce::String::fromUTF8(text).substring(0,max);}float getValueForText(const juce::String&s)const override{return juce::jlimit(0.0f,1.0f,s.getFloatValue());}
 };
 struct Editor final:public juce::AudioProcessorEditor,private juce::Timer {
  Instance&owner;juce::HWNDComponent view;HWND handle=nullptr;
  Editor(Instance&o):AudioProcessorEditor(o),owner(o){handle=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);view.setHWND(handle);addAndMakeVisible(view);owner.send(14,0,0,handle);struct Rect{short top,left,bottom,right;};Rect*r=nullptr;owner.send(13,0,0,&r);setSize(r?juce::jlimit(100,3000,int(r->right-r->left)):640,r?juce::jlimit(100,2000,int(r->bottom-r->top)):480);startTimerHz(20);}
  ~Editor()override{stopTimer();owner.send(15);view.setHWND(nullptr);if(IsWindow(handle))DestroyWindow(handle);}
  void resized()override{view.setBounds(getLocalBounds());}void timerCallback()override{owner.send(19);}
 };
public:
 intptr_t send(int op,int index=0,intptr_t value=0,void*ptr=nullptr,float opt=0)const{return fx->dispatch(fx,op,index,value,ptr,opt);}
 static std::unique_ptr<Instance>open(const juce::File&path,juce::String&error){auto p=std::unique_ptr<Instance>(new Instance(path));p->module=LoadLibraryW(path.getFullPathName().toWideCharPointer());if(!p->module){error="Cannot load 64-bit VST2 DLL (Windows error "+juce::String(int(GetLastError()))+")";return {};}
  auto entry=reinterpret_cast<Effect*(__cdecl*)(Call)>(GetProcAddress(p->module,"VSTPluginMain"));if(!entry)entry=reinterpret_cast<Effect*(__cdecl*)(Call)>(GetProcAddress(p->module,"main"));if(!entry){error="This DLL is not a VST2 plug-in";return {};}
  p->fx=entry(host);if(!p->fx||p->fx->magic!=0x56737450||!p->fx->dispatch){p->fx=nullptr;error="Invalid VST2 plug-in";return {};}
  p->fx->user=p.get();p->send(0);if(p->fx->inputs<1||p->fx->inputs>2||p->fx->outputs<1||p->fx->outputs>2||!p->fx->replace){error="VST2 stereo/mono effects with replacing processing are supported";return {};}
  p->setPlayConfigDetails(p->fx->inputs,p->fx->outputs,96000,960);for(int i=0;i<p->fx->parameters;++i)p->addHostedParameter(std::make_unique<Parameter>(*p,i));p->setLatencySamples(p->fx->latency);return p;
 }
 explicit Instance(const juce::File&p):file(p){}
 ~Instance()override{releaseResources();if(fx){send(1);fx=nullptr;}if(module)FreeLibrary(module);}
 const juce::String getName()const override{char name[256]{};send(45,0,0,name);return name[0]?juce::String::fromUTF8(name):file.getFileNameWithoutExtension();}
 void fillInPluginDescription(juce::PluginDescription&d)const override{d.name=getName();d.descriptiveName=d.name;d.pluginFormatName="VST";d.fileOrIdentifier=file.getFullPathName();d.uniqueId=fx->id;d.numInputChannels=fx->inputs;d.numOutputChannels=fx->outputs;d.isInstrument=false;}
 void prepareToPlay(double r,int n)override{releaseResources();rate=r;block=n;time.rate=r;inputs.setSize(fx->inputs,8192);outputs.setSize(fx->outputs,8192);send(10,0,0,nullptr,float(r));send(11,0,n);send(12,0,1);send(71);prepared=true;}
 void releaseResources()override{if(fx&&prepared){send(72);send(12,0,0);prepared=false;}}
 void processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&)override{int n=b.getNumSamples();inputs.clear(0,n);outputs.clear(0,n);for(int c=0;c<fx->inputs;++c)inputs.copyFrom(c,0,b,juce::jmin(c,b.getNumChannels()-1),0,n);fx->replace(fx,inputs.getArrayOfWritePointers(),outputs.getArrayOfWritePointers(),n);for(int c=0;c<b.getNumChannels();++c)b.copyFrom(c,0,outputs,juce::jmin(c,fx->outputs-1),0,n);time.position+=n;}
 bool hasEditor()const override{return true;}juce::AudioProcessorEditor*createEditor()override{return (fx->flags&1)?static_cast<juce::AudioProcessorEditor*>(new Editor(*this)):new juce::GenericAudioProcessorEditor(*this);}
 bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}double getTailLengthSeconds()const override{return 2;}
 int getNumPrograms()override{return juce::jmax(1,fx->programs);}int getCurrentProgram()override{return int(send(3));}void setCurrentProgram(int p)override{send(2,0,p);}const juce::String getProgramName(int)override{char text[256]{};send(5,0,0,text);return juce::String::fromUTF8(text);}void changeProgramName(int,const juce::String&)override{}
 void getStateInformation(juce::MemoryBlock&dest)override{juce::XmlElement state("VST2State");state.setAttribute("id",fx->id);if(fx->flags&32){void*data=nullptr;auto bytes=send(23,0,0,&data);if(bytes>0&&data)state.setAttribute("chunk",juce::MemoryBlock(data,size_t(bytes)).toBase64Encoding());}for(int i=0;i<fx->parameters;++i)state.setAttribute("p"+juce::String(i),double(fx->get(fx,i)));copyXmlToBinary(state,dest);}
 void setStateInformation(const void*data,int size)override{auto state=getXmlFromBinary(data,size);if(!state||!state->hasTagName("VST2State")||state->getIntAttribute("id")!=fx->id)return;juce::MemoryBlock chunk;auto text=state->getStringAttribute("chunk");if(text.isNotEmpty()&&chunk.fromBase64Encoding(text))send(24,0,intptr_t(chunk.getSize()),chunk.getData());else for(int i=0;i<fx->parameters;++i)fx->set(fx,i,float(state->getDoubleAttribute("p"+juce::String(i),fx->get(fx,i))));}
};
}


