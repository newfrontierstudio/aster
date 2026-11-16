#pragma once
#include <atomic>
#include <cmath>
#include <algorithm>
// Updated on every audio block, including while the window is hidden.
struct PeakHold {
 std::atomic<float>value{0},pending{0};
 static void maximum(std::atomic<float>&v,float peak){auto old=v.load(std::memory_order_relaxed);while(peak>old&&!v.compare_exchange_weak(old,peak,std::memory_order_relaxed)){} }
 void push(float peak){if(!std::isfinite(peak))return;maximum(value,peak);maximum(pending,peak);}
 float consume(){return pending.exchange(0,std::memory_order_relaxed);}
 float get()const{return value.load(std::memory_order_relaxed);}
 void reset(){value.store(0,std::memory_order_relaxed);pending.store(0,std::memory_order_relaxed);}
};

// Same one-second hold and 20 dB/s fall as inferno Studio; driven by elapsed time.
struct MeterBallistics {
 float level=-60,held=-60,remaining=0;
 void update(float gain,float dt){
  dt=std::max(0.0f,dt);float db=gain>0&&std::isfinite(gain)?std::max(-60.0f,20.0f*std::log10(gain)):-60.0f;
  level=std::max(db,level-20.0f*dt);
  if(db>=held&&gain>0){held=db;remaining=1.0f;}
  else {float falling=std::max(0.0f,dt-remaining);remaining=std::max(0.0f,remaining-dt);held=std::max(level,held-20.0f*falling);}
 }
};
