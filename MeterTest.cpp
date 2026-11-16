#include "PeakHold.h"
#include <cassert>
#include <iostream>
int main(){PeakHold p;p.push(1.25f);p.push(0.1f);assert(p.consume()==1.25f);assert(p.consume()==0);assert(p.get()==1.25f);p.push(NAN);assert(p.consume()==0);p.reset();assert(p.get()==0);MeterBallistics m;m.update(1.25f,0);assert(m.held>0);m.update(0,.5f);assert(m.held>0);m.update(0,.6f);assert(m.held<0);m.update(0,10);assert(m.held==-60&&m.level==-60);m.update(.5f,.01f);assert(m.level>-6.1f&&m.level<-6);std::cout<<"PASS: interval peak capture, historical peak, non-finite rejection, one-second hold, automatic recovery, silence floor, immediate attack\n";}
