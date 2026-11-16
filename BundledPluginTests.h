#pragma once
// Offline VST3 test: never opens an audio device or captures/reroutes an app.
static bool verifyBundledPlugins(){
 juce::Array<juce::var>results;bool passed=true;juce::ScopedNoDenormals noDenormals;
 for(int effect=0;effect<2;++effect){
  auto*result=new juce::DynamicObject();juce::var row(result);results.add(row);result->setProperty("plugin",bundledEffects()[size_t(effect)].name);
  Engine host;bool loaded=host.add(bundledEffectFile(effect));result->setProperty("loaded",loaded);if(!loaded){result->setProperty("error",host.error);passed=false;continue;}
  auto&plugin=*host.nodes.back()->plugin;juce::MidiBuffer midi;bool finite=true,signal=false;int combinations=0;
  for(double rate:{44100.0,48000.0,96000.0})for(int block:{128,512,1024}){
   plugin.releaseResources();plugin.setRateAndBufferSizeDetails(rate,block);plugin.prepareToPlay(rate,block);juce::AudioBuffer<float>buffer(2,block);
   for(int k=0;k<128;++k){for(int c=0;c<2;++c)for(int i=0;i<block;++i)buffer.setSample(c,i,float(.35*std::sin((k*block+i)*6.283185307179586*(c?577:997)/rate)));plugin.processBlock(buffer,midi);for(int c=0;c<2;++c)for(int i=0;i<block;++i){auto v=buffer.getSample(c,i);finite=finite&&std::isfinite(v);signal=signal||std::abs(v)>1e-6f;}}++combinations;
  }
  bool stateOk=false;auto&parameters=plugin.getParameters();if(!parameters.isEmpty()){
   juce::AudioProcessorParameter*parameter=nullptr;for(auto*p:parameters)if(!p->getName(100).containsIgnoreCase("bypass")){parameter=p;break;}
   if(parameter){juce::AudioBuffer<float>buffer(2,1024);buffer.clear();parameter->setValueNotifyingHost(.35f);plugin.processBlock(buffer,midi);juce::MemoryBlock saved;plugin.getStateInformation(saved);parameter->setValueNotifyingHost(.75f);plugin.processBlock(buffer,midi);plugin.setStateInformation(saved.getData(),int(saved.getSize()));plugin.processBlock(buffer,midi);stateOk=saved.getSize()>0&&std::abs(parameter->getValue()-.35f)<.01f;}
  }
  plugin.releaseResources();plugin.setRateAndBufferSizeDetails(48000,512);plugin.prepareToPlay(48000,512);juce::AudioBuffer<float>buffer(2,512),input(2,512);
  for(int c=0;c<2;++c)for(int i=0;i<512;++i)input.setSample(c,i,float(.35*std::sin(i*6.283185307179586*(c?577:997)/48000.0)));
  double best=1e9;for(int run=0;run<3;++run){auto begin=juce::Time::getMillisecondCounterHiRes();for(int k=0;k<4096;++k){for(int c=0;c<2;++c)buffer.copyFrom(c,0,input,c,0,512);plugin.processBlock(buffer,midi);}double seconds=(juce::Time::getMillisecondCounterHiRes()-begin)/1000.0;best=juce::jmin(best,seconds);}
  result->setProperty("finite",finite);result->setProperty("signal",signal);result->setProperty("rateBlockCombinations",combinations);result->setProperty("stateRoundTrip",stateOk);result->setProperty("offlineLoadPercent",100*best/(4096.0*512/48000));result->setProperty("benchmarkRate",48000);result->setProperty("benchmarkBlock",512);result->setProperty("latencySamples",plugin.getLatencySamples());passed=passed&&finite&&signal&&stateOk&&combinations==9;
 }
 auto*report=new juce::DynamicObject();juce::var output(report);report->setProperty("passed",passed);report->setProperty("audioDeviceOpened",false);report->setProperty("settingsChanged",false);report->setProperty("measurement","Offline processing plus buffer copy, best of 3; not total Aster CPU usage");report->setProperty("results",results);home().createDirectory();home().getChildFile("bundled-plugins-test.json").replaceWithText(juce::JSON::toString(output));return passed;
}
