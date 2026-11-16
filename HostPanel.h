#pragma once
class PeakMeter final:public juce::Component,public juce::SettableTooltipClient{
 MeterBallistics meters[2];bool output=false,mini=false;double lastFrame=0;
public:
 void clear(){for(auto&m:meters)m=MeterBallistics{};lastFrame=0;repaint();}
 void setOutput(){output=true;}
 void setMini(){mini=true;setTooltip("Post-effect sample peaks L/R. One-second hold, then decay. Bypassed effects show no activity.");}
 void push(float left,float right){double now=juce::Time::getMillisecondCounterHiRes();float dt=lastFrame>0?float((now-lastFrame)/1000.0):1.0f/30;lastFrame=now;auto oldL=meters[0],oldR=meters[1];meters[0].update(left,dt);meters[1].update(right,dt);if(isShowing()&&(oldL.level!=meters[0].level||oldL.held!=meters[0].held||oldR.level!=meters[1].level||oldR.held!=meters[1].held))repaint();}
 static juce::String heldText(float gain,bool isOutput){if(gain<=0.001f)return "HR --  |  Peak --";float peak=juce::Decibels::gainToDecibels(gain,-60.0f);return (gain>=1?(isOutput?juce::String("CLIP"):juce::String("0 dBFS")):"HR "+juce::String(-peak,1)+" dB")+"  |  Peak "+juce::String(peak,1);}
 void paint(juce::Graphics&g)override{
  g.setFont(11.5f);float start=mini?0.0f:16.0f;float bar=mini?float(getWidth()):float(getWidth()-210);float step=mini?3.5f:12.0f;float thickness=output?4.0f:2.0f;float offset=mini?2.0f:output?4.0f:5.0f;
  auto fraction=[](float db){return juce::jlimit(0.0f,1.0f,(db+60)/60);};
  for(int c=0;c<2;++c){float y=float(c)*step;auto&m=meters[c];
   if(!mini){g.setColour(juce::Colour(0xffadbcc8));g.drawText(c==0?"L":"R",0,int(y),14,12,juce::Justification::centredLeft);}
   g.setColour(juce::Colour(0xff0c141b));g.fillRoundedRectangle(start,y+offset,bar,thickness,thickness/2);
   g.setColour(juce::Colour(0xff8297a6));g.fillRoundedRectangle(start,y+offset,bar*fraction(m.level),thickness,thickness/2);
   if(m.level>-12){g.setColour(juce::Colour(0xff6fa9ca));g.fillRect(start+bar*0.8f,y+offset,bar*(fraction(juce::jmin(m.level,-3.0f))-0.8f),thickness);}
   if(m.level>-3){g.setColour(juce::Colour(0xffa4d5ed));g.fillRect(start+bar*0.95f,y+offset,bar*(fraction(m.level)-0.95f),thickness);}
   if(m.held>-60){g.setColour(juce::Colour(0xffdee9ef));g.fillRect(start+juce::jmin(bar-1,bar*fraction(m.held)),y+offset-(output?2.0f:1.0f),1.0f,output?8.0f:4.0f);}
   if(!mini){g.setColour(juce::Colour(m.held>=0?0xffc1e7f9:0xffc5d2db));g.drawText(heldText(juce::Decibels::decibelsToGain(m.held,-60.0f),output),int(bar+26),int(y),180,12,juce::Justification::centredLeft);}
  }
 }
};
// Host edits are journalled as complete gestures; removed plug-ins stay alive for Undo.
class EditHistory {
 std::deque<std::function<void()>>steps;
public:
 void push(std::function<void()>f){steps.push_back(std::move(f));if(steps.size()>10)steps.pop_front();}
 bool undo(){if(steps.empty())return false;auto f=std::move(steps.back());steps.pop_back();f();return true;}
 size_t size()const{return steps.size();}void clear(){steps.clear();}
};
class Panel final:public juce::Component,public juce::DragAndDropContainer,private juce::Timer{
public:
 Engine&engine;HostFont font;EditHistory history;
 juce::Slider trims[Engine::maxSources],master;juce::ToggleButton mute[Engine::maxSources],outputMute;
 juce::TextButton chooseApp[Engine::maxSources],removeInput[Engine::maxSources],addInput{"Add input"};juce::Viewport inputViewport;juce::Component inputRack;
 juce::TextButton start{"Start"},stop{"Stop"},save{"Save"},addVst{"Add plugin"},meterSwitch;juce::ComboBox outputDevice;
 juce::Label sourceNames[Engine::maxSources],status,instances[Engine::maxSources],chainSummary;
 PeakMeter inputMeters[Engine::maxSources],outputMeter;juce::Viewport viewport;juce::Component rack;juce::Label empty;
 struct NameLabel final:juce::Label,juce::DragAndDropTarget{
  int index=0;Panel*owner=nullptr;bool hover=false;
  void mouseDrag(const juce::MouseEvent&e)override{
   if(isBeingEdited())return;
   if(owner&&owner->isDragAndDropActive()){auto p=e.getEventRelativeTo(&owner->viewport).getPosition();owner->viewport.autoScroll(p.x,p.y,18,10);}
   if(owner&&e.getDistanceFromDragStart()>4&&!owner->isDragAndDropActive())owner->startDragging("aster-effect:"+juce::String(index),this);
  }
  bool isInterestedInDragSource(const SourceDetails&d)override{auto*source=dynamic_cast<NameLabel*>(d.sourceComponent.get());return source&&source->owner==owner&&source!=this;}
  void itemDragEnter(const SourceDetails&)override{hover=true;repaint();}void itemDragExit(const SourceDetails&)override{hover=false;repaint();}
  void itemDropped(const SourceDetails&d)override{hover=false;repaint();auto*source=dynamic_cast<NameLabel*>(d.sourceComponent.get());if(source&&source->owner==owner)owner->moveNode(source->index,index);}
  void paint(juce::Graphics&g)override{if(hover){g.setColour(juce::Colour(0xff334c57));g.fillRoundedRectangle(getLocalBounds().toFloat(),3);}juce::Label::paint(g);g.setFont(11);g.setColour(juce::Colour(0xffadbcc8));g.drawText(juce::String(index+1),0,0,20,getHeight(),juce::Justification::centredLeft);if(hover){g.setColour(juce::Colour(0xff80d5c5));g.fillRect(0,0,getWidth(),2);}}
 };
 struct Row{NameLabel name;PeakMeter meter;juce::TextButton open{"Edit"},up{juce::String::charToString(0x2191)},down{juce::String::charToString(0x2193)},remove{"Delete"};juce::ToggleButton active{"On"};};
 std::vector<std::unique_ptr<Row>>rows;std::vector<std::unique_ptr<PluginWindow>>windows;
 std::function<void()>onSave;
 Panel(Engine&e):engine(e){
  setLookAndFeel(&font);setSize(740,583);setWantsKeyboardFocus(true);
  for(int i=0;i<Engine::maxSources;++i){
   for(auto*c:std::initializer_list<juce::Component*>{&trims[i],&mute[i],&instances[i],&inputMeters[i],&sourceNames[i],&chooseApp[i],&removeInput[i]})inputRack.addAndMakeVisible(c);
   chooseApp[i].setButtonText(engine.appGroups[i].size()>1?"Apps":"App");chooseApp[i].setTooltip(engine.appGroups[i].joinIntoString("\n"));chooseApp[i].onClick=[this,i]{chooseSource(i);};removeInput[i].setButtonText("x");removeInput[i].setTooltip("Remove input");removeInput[i].onClick=[this,i]{engine.removeSource(i);history.clear();resized();};
   sourceNames[i].setComponentID("source-name");sourceNames[i].setJustificationType(juce::Justification::centredRight);sourceNames[i].setText(engine.sourceNames[i],juce::dontSendNotification);sourceNames[i].setEditable(false,true,false);sourceNames[i].setTooltip("Double-click to rename. The captured application does not change.");
   sourceNames[i].onTextChange=[this,i]{renameSource(i,sourceNames[i].getText());};
   trims[i].setRange(Engine::silenceDb,12,0.1);trims[i].setSliderStyle(juce::Slider::LinearHorizontal);trims[i].setTextBoxStyle(juce::Slider::TextBoxRight,false,72,18);trims[i].setTextValueSuffix(" dB");trims[i].setValue(engine.trims[i].load(),juce::dontSendNotification);trims[i].setTitle("Input "+juce::String(i+1)+" trim");
   bindSlider(trims[i],engine.trims[i]);
   mute[i].setButtonText("Mute");mute[i].setToggleState(engine.sourceMute[i],juce::dontSendNotification);
   mute[i].onClick=[this,i]{bool old=engine.sourceMute[i];history.push([this,i,old]{engine.sourceMute[i]=old;mute[i].setToggleState(old,juce::dontSendNotification);});engine.sourceMute[i]=mute[i].getToggleState();};
  }
  for(auto*c:std::initializer_list<juce::Component*>{&master,&outputMute,&start,&stop,&save,&status,&outputDevice,&addVst,&meterSwitch,&outputMeter,&viewport,&chainSummary,&inputViewport,&addInput})addAndMakeVisible(c);
  outputDevice.setTooltip("Audio output device. Windows default is used unless you choose another device.");refreshOutputChoices();outputDevice.onChange=[this]{auto id=outputDevice.getSelectedId();auto selected=id<=1?juce::String():outputDevice.getItemText(id);if(!engine.selectOutputDevice(selected)){juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,"Audio output",engine.error);refreshOutputChoices();return;}if(onSave)onSave();};
  inputViewport.setViewedComponent(&inputRack,false);inputViewport.setScrollBarsShown(true,false);addInput.onClick=[this]{int i=engine.addSource();if(i<0){juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,"Add input",engine.error);return;}history.clear();sourceNames[i].setText(engine.sourceNames[i],juce::dontSendNotification);trims[i].setValue(0,juce::dontSendNotification);mute[i].setToggleState(false,juce::dontSendNotification);resized();inputViewport.setViewPosition(0,100000);chooseSource(i);};
  outputMeter.setOutput();outputMeter.setTooltip("Post-output-trim sample peaks before the safety clamp. One-second hold, then decay.");
  viewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId,juce::Colour(0xff48515c));inputViewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId,juce::Colour(0xff48515c));viewport.setViewedComponent(&rack,false);viewport.setScrollBarsShown(true,false);rack.addAndMakeVisible(empty);empty.setText("Dry mix: add an effect to build your chain.",juce::dontSendNotification);
  master.setRange(Engine::silenceDb,0,0.1);master.setSliderStyle(juce::Slider::LinearHorizontal);master.setTextBoxStyle(juce::Slider::TextBoxRight,false,72,18);master.setTextValueSuffix(" dB");master.setValue(engine.masterDb.load(),juce::dontSendNotification);master.setTitle("Output level");bindSlider(master,engine.masterDb);
  outputMute.setButtonText("Output mute");outputMute.setToggleState(engine.outputMuted,juce::dontSendNotification);
  outputMute.onClick=[this]{bool old=engine.outputMuted;history.push([this,old]{engine.outputMuted=old;outputMute.setToggleState(old,juce::dontSendNotification);});engine.outputMuted=outputMute.getToggleState();};
  start.onClick=[this]{history.push([this]{engine.stop();});engine.start(false);};stop.onClick=[this]{bool wasRunning=engine.running,wasMuted=engine.outputMuted;history.push([this,wasRunning,wasMuted]{if(wasRunning)engine.start(!wasMuted);});engine.stop();};
  save.onClick=[this]{commitEditors();if(onSave)onSave();};save.addShortcut(juce::KeyPress('s',juce::ModifierKeys::ctrlModifier,0));
  addVst.onClick=[this]{chooseVst();};meterSwitch.onClick=[this]{changeMeterMode((engine.meterMode.load()+1)%3);};updateMeterSwitch();rebuild();startTimerHz(30);
 }
 ~Panel()override{stopTimer();commitEditors();windows.clear();history.clear();viewport.setViewedComponent(nullptr,false);inputViewport.setViewedComponent(nullptr,false);setLookAndFeel(nullptr);}
 bool keyPressed(const juce::KeyPress&key)override{if(key.getModifiers().isCtrlDown()&&!key.getModifiers().isShiftDown()&&(key.getKeyCode()=='Z'||key.getKeyCode()=='z')){commitEditors();bool ok=history.undo();if(ok){updateMeterSwitch();scheduleRebuild();}return true;}return false;}
 void bindSlider(juce::Slider&slider,std::atomic<float>&value){
  slider.textFromValueFunction=[](double v){return v<=Engine::silenceDb+0.0001?juce::String::fromUTF8("-∞"):juce::String(v,1);};
  slider.valueFromTextFunction=[](const juce::String&text){auto t=text.trim();if(t.contains(juce::String::fromUTF8("∞"))||t.containsIgnoreCase("inf")||t.equalsIgnoreCase("mute"))return double(Engine::silenceDb);return t.getDoubleValue();};slider.updateText();
  auto dragging=std::make_shared<bool>(false);auto initial=std::make_shared<float>(value.load());auto*s=&slider;auto*v=&value;
  auto remember=[this,s,v](float old){history.push([s,v,old]{*v=old;s->setValue(old,juce::dontSendNotification);});};
  slider.onDragStart=[dragging,initial,v]{*dragging=true;*initial=v->load();};
  slider.onDragEnd=[dragging,initial,v,remember]{if(std::abs(v->load()-*initial)>0.00001f)remember(*initial);*dragging=false;};
  slider.onValueChange=[dragging,s,v,remember]{float next=float(s->getValue()),old=v->load();if(next==old)return;if(!*dragging)remember(old);*v=next;};
 }
 int visibleInputs()const{int n=0;for(int i=0;i<engine.sourceCount;++i)if(engine.present[i])++n;return n;}
 int chainTop()const{return 60+juce::jmin(3,visibleInputs())*57+12;}
 void refreshOutputChoices(){auto selected=engine.preferredOutputDevice;outputDevice.clear(juce::dontSendNotification);outputDevice.addItem("Windows default",1);int selectedId=1,id=2;for(auto&name:engine.availableOutputDevices()){outputDevice.addItem(name,id);if(name==selected)selectedId=id;++id;}outputDevice.setSelectedId(selectedId,juce::dontSendNotification);}
 void applySource(int i,juce::String exe){auto apps=engine.appGroups[i];if(!apps.contains(juce::File(exe).getFileName(),true))apps.add(juce::File(exe).getFileName());applySources(i,apps);}
 void applySources(int i,const juce::StringArray&apps){if(!engine.selectSources(i,apps)){juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,"Input app",engine.error);return;}history.clear();for(int j=0;j<engine.sourceCount;++j)if(engine.present[j]){sourceNames[j].setText(engine.sourceNames[j],juce::dontSendNotification);chooseApp[j].setButtonText(engine.appGroups[j].size()>1?"Apps":"App");chooseApp[j].setTooltip(engine.appGroups[j].joinIntoString("\n")+"\nClick to choose applications");inputMeters[j].clear();}resized();}
 void browseSource(int i){
  using namespace Microsoft::WRL;ComPtr<IFileOpenDialog>dialog;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return;
  COMDLG_FILTERSPEC filter{L"Applications (*.exe)",L"*.exe"};dialog->SetFileTypes(1,&filter);dialog->SetTitle(L"Choose input application");dialog->SetOptions(FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR);
  if(FAILED(dialog->Show((HWND)getTopLevelComponent()->getWindowHandle())))return;ComPtr<IShellItem>item;PWSTR path=nullptr;if(SUCCEEDED(dialog->GetResult(&item))&&SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){juce::String exe(path);CoTaskMemFree(path);applySource(i,exe);}
 }
 void chooseSource(int i){
  juce::Component::SafePointer<Panel>safe(this);
  auto used=[safe,i](juce::String exe){if(!safe)return true;for(int j=0;j<safe->engine.sourceCount;++j)if(j!=i&&safe->engine.present[j]&&safe->engine.sourceUses(j,exe))return true;return false;};
  auto select=[safe,i](const juce::StringArray&apps){if(safe&&safe->engine.present[i])safe->applySources(i,apps);};
  auto browse=[safe,i]{if(safe&&safe->engine.present[i])safe->browseSource(i);};
  juce::DialogWindow::LaunchOptions options;options.content.setOwned(new InputPicker(engine.appGroups[i],used,select,browse));options.dialogTitle="Input application";options.dialogBackgroundColour=juce::Colour(0xff171d26);options.escapeKeyTriggersCloseButton=true;options.useNativeTitleBar=true;options.resizable=false;options.componentToCentreAround=this;options.launchAsync();
 }
 void renameSource(int i,juce::String name){name=name.trim();auto old=engine.sourceNames[i];if(name.isEmpty()||name==old){sourceNames[i].setText(old,juce::dontSendNotification);return;}history.push([this,i,old]{engine.sourceNames[i]=old;sourceNames[i].setText(old,juce::dontSendNotification);});engine.sourceNames[i]=name;}
 void renameNode(Engine::Node*node,juce::String name){name=name.trim();auto old=node->name;if(name.isEmpty()||name==old){scheduleRebuild();return;}history.push([this,node,old]{node->name=old;for(auto&w:windows)if(w->processor==node->plugin.get())w->setName(old);scheduleRebuild();});node->name=name;for(auto&w:windows)if(w->processor==node->plugin.get())w->setName(name);}
 void moveNode(int from,int to){if(from==to)return;auto*node=engine.nodes[size_t(from)].get();history.push([this,node,from]{engine.moveTo(engine.indexOf(node),from);scheduleRebuild();});engine.moveTo(from,to);scheduleRebuild();}
 void commitEditors(){for(auto&w:windows)if(w->onCommit)w->onCommit();}
 void openEditor(Engine::Node*node){
  for(auto&w:windows)if(w->processor==node->plugin.get()){w->setVisible(true);w->toFront(true);return;}
  auto before=std::make_shared<juce::MemoryBlock>();{const juce::ScopedLock lock(engine.devices.getAudioCallbackLock());node->plugin->getStateInformation(*before);}
  auto w=std::make_unique<PluginWindow>(*node);w->onCommit=[this,node,before]{if(engine.indexOf(node)<0)return;juce::MemoryBlock after;{const juce::ScopedLock lock(engine.devices.getAudioCallbackLock());node->plugin->getStateInformation(after);}if(after==*before)return;auto saved=*before;history.push([this,node,saved,before]{*before=saved;const juce::ScopedLock lock(engine.devices.getAudioCallbackLock());node->plugin->setStateInformation(saved.getData(),int(saved.getSize()));if(node->meterPlugin)node->meterPlugin->setStateInformation(saved.getData(),int(saved.getSize()));});*before=after;engine.syncMeterCopies();};windows.push_back(std::move(w));
 }
 void updateMeterSwitch(){const char*names[]={"Peaks: Chain","Peaks: Separate","Peaks: Off"};meterSwitch.setButtonText(names[engine.meterMode.load()]);meterSwitch.setSize(juce::Font("Segoe UI",12,0).getStringWidth(meterSwitch.getButtonText())+24,23);meterSwitch.setTooltip("Click to cycle Chain / Separate / Off. Separate uses independent effect copies on the original input and costs additional CPU. Off disables mini-meter processing.");for(auto&r:rows){r->meter.clear();r->meter.setVisible(engine.meterMode!=2);}}
 void changeMeterMode(int mode){int old=engine.meterMode;if(old==mode)return;commitEditors();if(!engine.setMeterMode(mode)){juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,"Mini peaks",engine.error);return;}history.push([this,old]{if(!engine.setMeterMode(old))juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,"Mini peaks",engine.error);updateMeterSwitch();});updateMeterSwitch();}
 void scheduleRebuild(){juce::Component::SafePointer<Panel>safe(this);juce::MessageManager::callAsync([safe]{if(safe)safe->rebuild();});}
 void rebuild(){
  rows.clear();for(int i=0;i<int(engine.nodes.size());++i){auto*node=engine.nodes[size_t(i)].get();auto r=std::make_unique<Row>();r->meter.setMini();r->name.index=i;r->name.owner=this;r->name.setEditable(false,true,false);r->name.setBorderSize(juce::BorderSize<int>(0,22,0,0));r->name.setTooltip("Double-click to rename; drag to reorder.");r->name.setMouseCursor(juce::MouseCursor::DraggingHandCursor);
   for(auto*c:std::initializer_list<juce::Component*>{&r->name,&r->meter,&r->open,&r->up,&r->down,&r->active,&r->remove})rack.addAndMakeVisible(c);
   r->meter.setVisible(engine.meterMode!=2);r->name.setText(node->name,juce::dontSendNotification);r->name.onTextChange=[this,node,i]{renameNode(node,rows[size_t(i)]->name.getText());};r->active.setToggleState(node->enabled,juce::dontSendNotification);
   r->active.onClick=[this,node,i]{bool old=node->enabled;history.push([this,node,old]{node->enabled=old;scheduleRebuild();});node->enabled=rows[size_t(i)]->active.getToggleState();};
   r->open.onClick=[this,node]{openEditor(node);};r->up.onClick=[this,i]{moveNode(i,i-1);};r->down.onClick=[this,i]{moveNode(i,i+1);};
   r->remove.onClick=[this,node]{commitEditors();windows.clear();int index=engine.indexOf(node);auto kept=std::make_shared<std::unique_ptr<Engine::Node>>(engine.take(index));history.push([this,kept,index]{engine.insert(index,std::move(*kept));scheduleRebuild();});scheduleRebuild();};
   r->up.setEnabled(i>0);r->down.setEnabled(i+1<int(engine.nodes.size()));rows.push_back(std::move(r));
  }empty.setVisible(rows.empty());resized();
 }
 void chooseVst(){
  juce::PopupMenu menu;menu.addSectionHeader("Included VST3 effects");for(int i=0;i<2;++i)menu.addItem(i+1,bundledEffects()[size_t(i)].name);menu.addSeparator();menu.addItem(3,"Choose VST file...");juce::Component::SafePointer<Panel> safe(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addVst),[safe](int choice){if(!safe)return;if(choice==3)safe->chooseVstFile();else if(choice>=1&&choice<=2)safe->addPluginFile(bundledEffectFile(choice-1));});
 }
 void addPluginFile(const juce::File&file){commitEditors();if(engine.add(file)){auto*node=engine.nodes.back().get();history.push([this,node]{commitEditors();windows.clear();engine.remove(engine.indexOf(node));scheduleRebuild();});if(engine.meterMode==1&&!engine.setMeterMode(1)){engine.setMeterMode(0);updateMeterSwitch();}rebuild();}else juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,"Add plugin",engine.error);}
 void chooseVstFile(){
  using Microsoft::WRL::ComPtr;ComPtr<IFileOpenDialog> dialog;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return;
  COMDLG_FILTERSPEC filters[]={{L"VST plug-ins (*.vst3; *.dll)",L"*.vst3;*.dll"},{L"VST3 plug-ins (*.vst3)",L"*.vst3"},{L"VST2 plug-ins (*.dll)",L"*.dll"}};
  dialog->SetFileTypes(3,filters);dialog->SetTitle(L"Add plugin effect");dialog->SetOptions(FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR);
  ComPtr<IShellItem> folder;if(SUCCEEDED(SHCreateItemFromParsingName(L"C:\\Program Files\\Common Files\\VST3",nullptr,IID_PPV_ARGS(&folder))))dialog->SetDefaultFolder(folder.Get());
  if(FAILED(dialog->Show((HWND)getTopLevelComponent()->getWindowHandle())))return;
  ComPtr<IShellItem> item;PWSTR path=nullptr;if(SUCCEEDED(dialog->GetResult(&item))&&SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){juce::File file{juce::String(path)};CoTaskMemFree(path);addPluginFile(file);}
 }
 void paint(juce::Graphics&g)override{
  g.fillAll(juce::Colour(0xff171d26));g.setColour(juce::Colour(0xff80d5c5));juce::Path icon;icon.addStar({46,25},4,4,9,0.4f);g.fillPath(icon);g.setColour(juce::Colours::white);g.setFont(23);g.drawText("aster",66,7,65,35,juce::Justification::centredLeft);g.setFont(14);g.setColour(juce::Colour(0xffadbcc8));g.drawText("0.3.18",130,11,70,31,juce::Justification::centredLeft);g.setColour(juce::Colours::white);g.setFont(14);g.drawText("SHARED EFFECT CHAIN",40,chainTop(),180,24,juce::Justification::centredLeft);g.drawText("Output level",40,getHeight()-79,140,24,juce::Justification::centredLeft);
 }
 void resized()override{
  int right=getWidth()-40,n=0;int inputHeight=juce::jmin(3,visibleInputs())*57;inputViewport.setBounds(38,58,getWidth()-76,inputHeight);int width=inputViewport.getWidth()-16;
  for(int i=0;i<Engine::maxSources;++i){bool visible=engine.present[i];for(auto*c:std::initializer_list<juce::Component*>{&sourceNames[i],&trims[i],&mute[i],&instances[i],&inputMeters[i],&chooseApp[i],&removeInput[i]})c->setVisible(visible);if(!visible)continue;int y=n++*57;sourceNames[i].setBounds(0,y,137,25);chooseApp[i].setBounds(141,y+2,43,22);trims[i].setBounds(194,y+2,juce::jmax(170,width-365),22);mute[i].setBounds(width-164,y+2,65,22);instances[i].setBounds(width-98,y+2,74,22);removeInput[i].setBounds(width-22,y+2,22,22);inputMeters[i].setBounds(194,y+27,width-194,24);}
  inputRack.setSize(width,juce::jmax(1,n*57));int inputButtonWidth=juce::Font("Segoe UI",12,0).getStringWidth(addInput.getButtonText())+24;addInput.setBounds(getWidth()-40-inputButtonWidth,15,inputButtonWidth,23);addInput.setEnabled(visibleInputs()<Engine::maxSources);
  int top=chainTop(),footer=getHeight()-79;viewport.setBounds(40,top+33,getWidth()-80,juce::jmax(60,footer-top-59));rack.setSize(viewport.getMaximumVisibleWidth(),juce::jmax(34,int(rows.size())*34));
  for(int i=0;i<int(rows.size());++i){auto&r=*rows[size_t(i)];int y=i*34;r.name.setBounds(0,y,270,24);r.meter.setBounds(280,y+8,120,9);r.active.setBounds(415,y,50,24);r.open.setBounds(475,y+2,44,21);r.up.setBounds(527,y+2,23,21);r.down.setBounds(558,y+2,23,21);r.remove.setBounds(589,y+2,51,21);}
  empty.setBounds(8,4,640,24);int pluginButtonWidth=juce::Font("Segoe UI",12,0).getStringWidth(addVst.getButtonText())+24;int meterButtonWidth=juce::Font("Segoe UI",12,0).getStringWidth(meterSwitch.getButtonText())+24;addVst.setBounds(230,chainTop()+1,pluginButtonWidth,23);meterSwitch.setBounds(addVst.getRight()+6,chainTop()+1,meterButtonWidth,23);master.setBounds(190,footer,310,22);outputMute.setBounds(510,footer,140,22);outputMeter.setBounds(190,footer+32,right-190,24);chainSummary.setBounds(40,footer-18,right-40,18);
  int headerX=200;for(auto*button:{&start,&stop,&save}){int w=juce::Font("Segoe UI",12,0).getStringWidth(button->getButtonText())+24;button->setBounds(headerX,15,w,23);headerX+=w+6;}int outputX=headerX+4;outputDevice.setBounds(outputX,15,juce::jmax(140,juce::jmin(220,addInput.getX()-outputX-12)),23);status.setBounds(200,39,juce::jmax(100,addInput.getX()-210),17);
 }
private:
 MeterBallistics chainDisplay;double chainClock=0;juce::uint32 lastLabels=0,lastWrite=0,lastSync=0;
 void timerCallback()override{
  engine.miniVisible=isShowing();engine.refreshSources();auto now=juce::Time::getMillisecondCounter();
  if(isShowing()){
   for(int i=0;i<engine.sourceCount;++i)if(engine.present[i])inputMeters[i].push(engine.sourceHolds[i][0].consume(),engine.sourceHolds[i][1].consume());outputMeter.push(engine.outputHolds[0].consume(),engine.outputHolds[1].consume());
   if(engine.meterMode!=2)for(size_t i=0;i<rows.size();++i)rows[i]->meter.push(engine.nodes[i]->meters[0].consume(),engine.nodes[i]->meters[1].consume());
   double clock=juce::Time::getMillisecondCounterHiRes();chainDisplay.update(engine.chainHold.consume(),chainClock>0?float((clock-chainClock)/1000):1.0f/30);chainClock=clock;auto peak=juce::Decibels::decibelsToGain(chainDisplay.held,-60.0f);auto text="Before output trim: "+PeakMeter::heldText(peak,false);if(chainSummary.getText()!=text)chainSummary.setText(text,juce::dontSendNotification);
  }
  if(now-lastSync>=500&&engine.meterMode==1){lastSync=now;if(std::any_of(windows.begin(),windows.end(),[](const std::unique_ptr<PluginWindow>&w){return w->isVisible();}))engine.syncMeterCopies();}
  if(now-lastLabels>=1000){lastLabels=now;engine.sampleCpu();start.setEnabled(!engine.running);stop.setEnabled(engine.running);outputMute.setToggleState(engine.outputMuted,juce::dontSendNotification);for(int i=0;i<engine.sourceCount;++i){if(!engine.present[i])continue;auto count=engine.captures[i]->instances.load();instances[i].setText(juce::String(count)+(count==1?" instance":" instances")+(count==0?(engine.appGroups[i].isEmpty()?" - no apps":" - waiting"):""),juce::dontSendNotification);}juce::String text=juce::String(int(engine.sampleRate/1000))+" kHz | "+juce::String(engine.blockSize)+" samples | Audio "+juce::String(int(std::round(engine.devices.getCpuUsage()*100)))+"% | App "+juce::String(engine.appCpuPct,1)+"% | ";text+=engine.running?(engine.outputMuted?"Running - muted":"Running"):"Stopped";if(engine.error.isNotEmpty())text+=" | "+engine.error;status.setTooltip(text);status.setText(text,juce::dontSendNotification);}
  if(now-lastWrite>=5000){lastWrite=now;home().getChildFile("status.json").replaceWithText(juce::JSON::toString(engine.snapshot()));}
 }
};
