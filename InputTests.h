#pragma once
static bool verifyInputs(){
 auto check=[](bool ok,const char*name){if(!ok)throw std::runtime_error(name);};
 try{
  check(Engine::levelGain(-90)>0&&Engine::levelGain(-60)>0&&Engine::levelGain(Engine::silenceDb)==0,"Finite -90dB remains audible in math; infinity endpoint is exactly zero");
  std::vector<aster::AppProcess> all{{1,0,L"chrome.exe"},{2,1,L"chrome.exe"},{3,0,L"chrome.exe"},{4,0,L"mpc-be64.exe"},{5,0,L"mpc-be.exe"},{6,0,L"player.exe"},{7,6,L"helper.exe"}};
  auto roots=aster::selectedRoots(all,{L"chrome.exe",L"mpc-be64.exe",L"player.exe",L"helper.exe"},999);
  check(roots[0]==std::vector<DWORD>{1,3},"Browser descendants must not be captured twice");
  check(roots[1]==std::vector<DWORD>{4,5},"Both MPC executable names must remain supported");
  check(roots[2]==std::vector<DWORD>{6}&&roots[3].empty(),"Cross-input parent/child trees must be captured once");
  check(aster::selectedRoots(all,{L"player.exe",L"helper.exe"},7)[0].empty(),"Do not capture a tree containing the host");
  check(aster::selectedRoots(all,{L"chrome.exe",L"CHROME.EXE"},999)[1].empty(),"Duplicate saved inputs must not double audio");
  all={{81,0,L"player.exe"}};check(aster::selectedRoots(all,{L"player.exe"},999)[0]==std::vector<DWORD>{81},"A new PID must be selected after restart");
  Engine e;auto config=juce::JSON::parse(R"({"nodes":[],"chromeTrimDb":-12,"mpcTrimDb":0,"masterDb":-8.721892,"chromeName":"Browser","mpcName":"Player"})");
  check(e.load(config)&&e.sourceNames[0]=="Browser"&&e.trims[0]==-12,"Migrate existing input names and gains");
  int i=e.addSource();check(i==2&&e.selectSource(i,"D:/Apps/player.exe"),"Third input selection");
  e.trims[i]=-4;e.sourceMute[i]=true;
  check(!e.selectSources(i,juce::StringArray{"radio.exe","not-an-executable"})&&e.exes[i]=="player.exe","Invalid selection cannot partially change routes");
  auto own=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFileName();check(!e.selectSource(i,own),"Reject host selection");
  e.error.clear();e.save("test");Engine restored;check(restored.load(juce::JSON::parse(home().getChildFile("settings.json"))),"Reload inputs");
  check(restored.sourceCount==3&&restored.exes[2]=="player.exe"&&restored.trims[2]==-4&&restored.sourceMute[2]&&std::abs(restored.masterDb.load()+8.721892f)<.0001f,"Persist app selection, gain, mute and master");
  check(restored.selectSources(2,juce::StringArray{"player.exe","radio.exe"})&&restored.appGroups[2].size()==2&&restored.trims[2]==-4&&restored.sourceMute[2],"Multiple apps share the existing channel gain and mute");
  check(restored.selectSources(2,juce::StringArray{"radio.exe","chrome.exe"})&&restored.sourceUses(2,"chrome.exe")&&!restored.sourceUses(0,"chrome.exe")&&restored.trims[0]==-12&&restored.trims[2]==-4,"Apply moves existing apps to the new channel while preserving both gains");
  check(restored.appGroups[0].isEmpty()&&restored.sourceUses(2,"radio.exe"),"The old channel stays present and is no longer capturing the moved app");
  restored.save("group-test");Engine grouped;check(grouped.load(juce::JSON::parse(home().getChildFile("settings.json")))&&grouped.appGroups[2].size()==2&&grouped.sourceUses(2,"radio.exe")&&grouped.trims[2]==-4&&grouped.sourceMute[2],"Group selections and channel controls survive reload");
  check(grouped.selectSources(2,{})&&grouped.appGroups[2].isEmpty()&&grouped.trims[2]==-4,"Clearing the app group keeps channel gain");
  InputPicker picker(juce::StringArray{"player.exe"},[](juce::String exe){return exe=="blocked.exe";},[](const juce::StringArray&){},[]{});
  check(picker.toggleApp("radio.exe")&&picker.selectedApplications().size()==2,"Picker supports independent checkmarks");
  picker.searchFor("no-such-application-filter");check(picker.selectedApplications().size()==2,"Search preserves hidden checkmarks");
  check(picker.toggleApp("blocked.exe")&&picker.selectedApplications().size()==3,"Apps assigned elsewhere remain selectable for a move");
  check(picker.toggleApp("player.exe")&&picker.selectedApplications().size()==2,"Picker supports unchecking group members");
  restored.removeSource(1);check(!restored.present[1]&&restored.addSource()==1,"Removed slots can be reused");
  for(int n=0;n<Engine::maxSources;++n)restored.addSource();check(restored.addSource()==-1,"Bound input capacity safely");
  Engine levels;check(levels.load(config),"Create level persistence fixture");levels.trims[0]=-90;levels.trims[1]=Engine::silenceDb;levels.masterDb=Engine::silenceDb;levels.save("level-test");Engine restoredLevels;check(restoredLevels.load(juce::JSON::parse(home().getChildFile("settings.json")))&&restoredLevels.trims[0]==-90&&restoredLevels.trims[1]==Engine::silenceDb&&restoredLevels.masterDb==Engine::silenceDb,"Finite and infinity gains survive save/reload");
  {Panel panel(restoredLevels);auto infinity=juce::String::fromUTF8("∞");check(panel.trims[0].getTextFromValue(Engine::silenceDb).contains(infinity)&&panel.master.getTextFromValue(Engine::silenceDb).contains(infinity),"Input and output display infinity at their endpoints");check(!panel.master.getTextFromValue(Engine::silenceDb).contains("dB dB")&&!panel.trims[0].getTextFromValue(-90).contains("dB dB"),"Gain labels contain the unit once");check(panel.master.getValueFromText("-inf")==double(Engine::silenceDb)&&panel.trims[0].getValueFromText("-90 dB")==-90,"Infinity and finite numeric entry are distinct");auto image=panel.createComponentSnapshot(panel.getLocalBounds());auto stream=home().getChildFile("silence-slider-preview.png").createOutputStream();stream->setPosition(0);juce::PNGImageFormat().writeImageToStream(image,*stream);stream->truncate();}
  home().getChildFile("input-test.txt").replaceWithText("PASS: input migration/move/group/persistence, process deduplication, extended gain range, infinity persistence/display and numeric entry");return true;
 }catch(const std::exception&e){home().getChildFile("input-test.txt").replaceWithText(e.what());return false;}
}
