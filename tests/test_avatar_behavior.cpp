#include <assert.h>
#include "../firmware/avatar_behavior.h"
using namespace avatar_behavior;
int main() {
 Status s={2,0,1,true,false};
 assert(select(s,false,true)==Mode::working);
 assert(select(s,true,true)==Mode::finished);
 s.attention=1;assert(select(s,true,true)==Mode::waiting);
 s.healthy=false;assert(select(s,true,true)==Mode::offline);
 assert(select(s,true,true,true)==Mode::sleeping);
 s.listening=true;assert(select(s,true,true,true)==Mode::listening);
 s={0,0,0,true,false};
 assert(select(s,false,true)==Mode::idle);
 assert(select(s,false,false)==Mode::sleeping);
 s.done=99;assert(select(s,false,false)==Mode::finished);
 assert(pending(UINT32_MAX-50,100));assert(!pending(101,100));
 for(unsigned i=0;i<eye_acting::kStateCount;++i) {
  const auto state=static_cast<eye_acting::State>(i);
  for(int j=0;j<eye_acting::clipCount(state);++j) {
   const auto& c=eye_acting::clip(state,j);
   assert(c.count>1&&c.keys[0].ms==0);
   for(int k=1;k<c.count;++k)assert(c.keys[k].ms>c.keys[k-1].ms);
   if(eye_acting::lively(c))assert(state==eye_acting::State::done);
  }
 }
 const auto pose=eye_acting::withBody(eye_acting::withLids(eye_acting::withGaze(eye_acting::p(),-8,3),.2f,.1f),-2,1);
 assert(pose.x==-8&&pose.y==3&&pose.upperL==.2f&&pose.lean==-2&&pose.lift==1);
}
