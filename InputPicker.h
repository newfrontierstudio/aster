#pragma once
#include <audiopolicy.h>
#include <dwmapi.h>
#include <map>

struct InputApp { juce::String exe,title;bool audio=false,used=false; };
inline std::vector<InputApp> inputApplications(){
 auto processes=aster::enumerateAppProcesses();std::map<DWORD,juce::String> windows;
 EnumWindows([](HWND w,LPARAM data)->BOOL{
  if(!IsWindowVisible(w)||GetWindow(w,GW_OWNER))return TRUE;
  DWORD cloaked=0;DwmGetWindowAttribute(w,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));if(cloaked)return TRUE;
  wchar_t title[1024]{};if(!GetWindowTextW(w,title,1024))return TRUE;
  DWORD pid=0;GetWindowThreadProcessId(w,&pid);auto&map=*reinterpret_cast<std::map<DWORD,juce::String>*>(data);if(!map.count(pid))map[pid]=juce::String(title);return TRUE;
 },reinterpret_cast<LPARAM>(&windows));
 std::vector<DWORD>audio;
 using Microsoft::WRL::ComPtr;ComPtr<IMMDeviceEnumerator>devices;ComPtr<IMMDeviceCollection>outputs;
 if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&devices)))&&SUCCEEDED(devices->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&outputs))){
  UINT count=0;outputs->GetCount(&count);for(UINT i=0;i<count;++i){ComPtr<IMMDevice>device;ComPtr<IAudioSessionManager2>manager;ComPtr<IAudioSessionEnumerator>sessions;
   if(FAILED(outputs->Item(i,&device))||FAILED(device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(manager.GetAddressOf())))||FAILED(manager->GetSessionEnumerator(&sessions)))continue;
   int n=0;sessions->GetCount(&n);for(int j=0;j<n;++j){ComPtr<IAudioSessionControl>control;ComPtr<IAudioSessionControl2>session;DWORD pid=0;AudioSessionState state=AudioSessionStateExpired;
    if(SUCCEEDED(sessions->GetSession(j,&control))&&SUCCEEDED(control.As(&session))&&session->IsSystemSoundsSession()!=S_OK&&SUCCEEDED(session->GetProcessId(&pid))&&SUCCEEDED(session->GetState(&state))&&state!=AudioSessionStateExpired&&pid)audio.push_back(pid);
   }
  }
 }
 // Attribute helper/browser audio sessions to their visible application when possible.
 for(auto&pid:audio){DWORD parent=pid;for(int depth=0;depth<64&&parent;++depth){if(windows.count(parent)){pid=parent;break;}auto it=std::find_if(processes.begin(),processes.end(),[&](const aster::AppProcess&p){return p.pid==parent;});if(it==processes.end()||it->parent==parent)break;parent=it->parent;}}
 std::vector<InputApp> result;
 for(auto&p:processes){if(p.pid<=4||p.pid==GetCurrentProcessId())continue;bool hasAudio=std::find(audio.begin(),audio.end(),p.pid)!=audio.end();auto window=windows.find(p.pid);if(!hasAudio&&window==windows.end())continue;
  juce::String exe(p.exe.c_str());auto it=std::find_if(result.begin(),result.end(),[&](const InputApp&a){return a.exe.equalsIgnoreCase(exe);});
  if(it==result.end())result.push_back({exe,window==windows.end()?exe.upToLastOccurrenceOf(".",false,false):window->second,hasAudio,false});
  else{it->audio|=hasAudio;if(window!=windows.end())it->title=window->second;}
 }
 std::stable_sort(result.begin(),result.end(),[](const InputApp&a,const InputApp&b){if(a.audio!=b.audio)return a.audio>b.audio;return a.title.compareIgnoreCase(b.title)<0;});return result;
}


class InputPicker final:public juce::Component,public juce::ListBoxModel {
 juce::TextEditor search;juce::ListBox list{"Applications",this};juce::TextButton select{"Apply"},browse{"Choose .exe..."},refresh{"Refresh"},cancel{"Cancel"};
 juce::Label count;std::vector<InputApp>apps;std::vector<int>filtered;juce::StringArray checked;
 std::function<void(const juce::StringArray&)>selected;std::function<bool(juce::String)>alreadyUsed;
 bool contains(juce::String exe)const{for(auto&app:checked)if(aster::sameExecutable(app.toWideCharPointer(),exe.toWideCharPointer()))return true;return false;}
 void close(){if(auto*w=findParentComponentOfClass<juce::DialogWindow>())w->exitModalState(0);}
 void updateCount(){count.setText(juce::String(checked.size())+" selected — Apply moves checked apps into this input",juce::dontSendNotification);}
 void filter(){filtered.clear();auto q=search.getText().trim();for(int i=0;i<int(apps.size());++i)if(q.isEmpty()||apps[i].title.containsIgnoreCase(q)||apps[i].exe.containsIgnoreCase(q))filtered.push_back(i);list.deselectAllRows();list.updateContent();updateCount();}
 void reload(){apps=inputApplications();for(auto&a:apps)a.used=alreadyUsed(a.exe);for(auto&exe:checked){bool found=false;for(auto&a:apps)if(aster::sameExecutable(a.exe.toWideCharPointer(),exe.toWideCharPointer()))found=true;if(!found)apps.push_back({exe,exe+" (waiting for app)",false,alreadyUsed(exe)});}filter();}
 void accept(){selected(checked);close();}
 void browseExecutable(){
  using Microsoft::WRL::ComPtr;ComPtr<IFileOpenDialog>dialog;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return;
  COMDLG_FILTERSPEC spec{L"Applications (*.exe)",L"*.exe"};dialog->SetFileTypes(1,&spec);dialog->SetOptions(FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR);dialog->SetTitle(L"Add application to this input");
  if(FAILED(dialog->Show((HWND)getTopLevelComponent()->getWindowHandle())))return;ComPtr<IShellItem>item;PWSTR path=nullptr;if(SUCCEEDED(dialog->GetResult(&item))&&SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){juce::String exe=juce::File(juce::String(path)).getFileName();CoTaskMemFree(path);if(!contains(exe))checked.add(exe);reload();}
 }
public:
 InputPicker(const juce::StringArray&values,std::function<bool(juce::String)>used,std::function<void(const juce::StringArray&)>onSelect,std::function<void()>):checked(values),selected(std::move(onSelect)),alreadyUsed(std::move(used)){
  setSize(600,440);for(auto*c:std::initializer_list<juce::Component*>{&search,&list,&select,&browse,&refresh,&cancel,&count})addAndMakeVisible(c);
  search.setColour(juce::TextEditor::backgroundColourId,juce::Colour(0xff0f171f));search.setColour(juce::TextEditor::textColourId,juce::Colours::white);list.setColour(juce::ListBox::backgroundColourId,juce::Colour(0xff171d26));count.setColour(juce::Label::textColourId,juce::Colour(0xffadbcc8));
  search.setFont(juce::Font("Meiryo",14,juce::Font::plain));search.setTextToShowWhenEmpty("Search app or window name",juce::Colours::grey);search.onTextChange=[this]{filter();};search.onReturnKey=[this]{accept();};list.setRowHeight(50);select.onClick=[this]{accept();};browse.onClick=[this]{browseExecutable();};refresh.onClick=[this]{reload();};cancel.onClick=[this]{close();};reload();
 }
 InputPicker(juce::String value,std::function<bool(juce::String)>used,std::function<void(juce::String)>onSelect,std::function<void()>onBrowse):InputPicker(value.isEmpty()?juce::StringArray():juce::StringArray{value},std::move(used),[onSelect](const juce::StringArray&apps){onSelect(apps.isEmpty()?juce::String():apps[0]);},std::move(onBrowse)){}
 void searchFor(juce::String query){search.setText(query);filter();}
 juce::StringArray selectedApplications()const{return checked;}
 bool toggleApp(juce::String exe){if(contains(exe)){for(int n=checked.size();--n>=0;)if(aster::sameExecutable(checked[n].toWideCharPointer(),exe.toWideCharPointer()))checked.remove(n);}else checked.add(exe);updateCount();list.repaint();return true;}
 ~InputPicker()override{list.setModel(nullptr);}
 int getNumRows()override{return int(filtered.size());}
 void listBoxItemClicked(int row,const juce::MouseEvent&)override{if(row>=0&&row<int(filtered.size()))toggleApp(apps[filtered[row]].exe);}
 void paintListBoxItem(int row,juce::Graphics&g,int width,int height,bool chosen)override{if(row<0||row>=int(filtered.size()))return;const auto&a=apps[filtered[row]];g.fillAll(chosen?juce::Colour(0xff334c57):juce::Colour(0xff171d26));g.setColour(juce::Colour(0xff80d5c5));g.drawRect(10,13,15,15);if(contains(a.exe)){juce::Path tick;tick.startNewSubPath(12,20);tick.lineTo(17,25);tick.lineTo(24,15);g.strokePath(tick,juce::PathStrokeType(2));}g.setColour(juce::Colours::white);g.setFont(juce::Font("Meiryo",14,juce::Font::plain));g.drawText(a.title,36,3,width-46,23,juce::Justification::centredLeft,true);g.setColour(juce::Colour(0xffadbcc8));g.setFont(11);g.drawText(a.exe+(a.used?" — moves here on Apply":a.audio?" — audio":""),36,26,width-46,height-28,juce::Justification::centredLeft,true);}
 void resized()override{auto area=getLocalBounds().reduced(14);auto top=area.removeFromTop(28);refresh.setBounds(top.removeFromRight(80));search.setBounds(top.withTrimmedRight(8));area.removeFromTop(8);auto bottom=area.removeFromBottom(28);cancel.setBounds(bottom.removeFromRight(76));bottom.removeFromRight(8);select.setBounds(bottom.removeFromRight(76));browse.setBounds(bottom.removeFromLeft(115));area.removeFromBottom(8);count.setBounds(area.removeFromBottom(24));list.setBounds(area);}
 void paint(juce::Graphics&g)override{g.fillAll(juce::Colour(0xff171d26));}
};
