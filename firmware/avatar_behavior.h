#pragma once
#include "eye_life.h"

// Shared semantic policy for hardware and desktop. No I/O, task mutation or
// platform power decisions: adapters supply whether idle/offline may sleep.
namespace avatar_behavior {
enum class Mode : uint8_t { offline, idle, sleeping, working, waiting, finished, listening };
struct Status {
 int active,attention,done;
 bool healthy,listening;
};
inline bool pending(uint32_t now,uint32_t deadline) {
 return static_cast<int32_t>(deadline-now)>0;
}
// USB-connected visual sleep may be startled; battery sleep never listens.
inline bool canStartle(Mode mode,bool unplugged,bool recording,bool staged,bool button) {
 return !unplugged&&!recording&&mode!=Mode::listening&&!staged&&!button;
}
inline Mode select(const Status& s,bool celebrating,bool idleAwake,bool disconnectedSleep=false,
                   bool startled=false) {
 if(s.listening)return Mode::listening;
 if(!s.healthy)return disconnectedSleep&&!startled?Mode::sleeping:Mode::offline;
 if(s.attention>0)return Mode::waiting;
 if(celebrating||(s.done>0&&s.active==0))return Mode::finished;
 if(s.active>0)return Mode::working;
 return idleAwake||startled?Mode::idle:Mode::sleeping;
}
inline bool scripted(Mode mode) {
 return mode==Mode::working||mode==Mode::waiting||mode==Mode::finished||mode==Mode::listening;
}
inline eye_acting::State state(Mode mode) {
 return mode==Mode::listening?eye_acting::State::listening:
        mode==Mode::waiting?eye_acting::State::attention:
        mode==Mode::finished?eye_acting::State::done:eye_acting::State::thinking;
}
inline const eye_life::Mood& mood(Mode mode) {
 if(mode==Mode::working)return eye_life::kThinking;
 if(mode==Mode::finished)return eye_life::kDone;
 if(mode==Mode::waiting)return eye_life::kAttention;
 if(mode==Mode::listening)return eye_life::kListening;
 if(mode==Mode::offline)return eye_life::kOffline;
 return eye_life::kIdle;
}
inline eye_acting::Pose rest(Mode mode) {
 if(mode==Mode::sleeping)return eye_acting::p(0,4,1,1,0,0,0,1,1,1,1,0,0,.35f);
 if(mode==Mode::offline)return eye_acting::p(0,4,1,.9f,.2f,.2f);
 if(mode==Mode::waiting)return eye_acting::p(0,0,1.10f,1.15f);
 if(mode==Mode::finished)return eye_acting::p(0,0,1,1,0,0,.45f);
 if(mode==Mode::listening)return eye_acting::p(0,-2,1,1.08f);
 return eye_acting::p();
}
} // namespace avatar_behavior
