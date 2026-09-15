#include "../src/thermal_guard.h"
#include <iostream>
#include <cstdlib>
using namespace cxnthermal;
static int checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<"FAIL line "<<__LINE__<<": "<<#x<<"\n";return 1;}}while(0)
int main(){
 Guard g;
 CHECK(!g.update(true,false,200,70,false,200,85));CHECK(!g.fanFull(true));
 CHECK(!g.update(true,true,70,70,true,84.9f,85));CHECK(g.moduleCount==1&&g.fanFull(true));
 CHECK(!g.update(true,true,69.9f,70,true,20,85));CHECK(g.moduleCount==0&&!g.fanFull(true));
 CHECK(!g.update(true,false,0,70,true,85,85));CHECK(g.esp32Count==1);
 CHECK(g.update(true,false,0,70,true,86,85));CHECK(g.latched&&g.source==Source::Esp32&&g.fanFull(true));
 CHECK(!g.update(true,true,100,70,true,100,85));CHECK(g.source==Source::Esp32); // only one trigger/source
 CHECK(!g.update(false,true,100,70,true,100,85));CHECK(g.moduleCount==0&&g.esp32Count==0&&!g.fanFull(false));
 g.reset();CHECK(!g.latched&&g.source==Source::None);
 CHECK(!g.update(true,true,71,70,true,90,85));CHECK(g.update(true,true,71,70,true,90,85));CHECK(g.source==Source::Module);
 std::cout<<"PASS "<<checks<<" thermal guard assertions\n";
}
