#pragma once
#include <stddef.h>
#include <stdint.h>

// Portable, versioned timing-only diagnostics. T is the existing diagnostics
// kind; no new audio/control protocol and no task identifiers/counts/text.
namespace frame_profile {
enum Metric { interval,update,background,mask,face,eyes,mouth,attention,badges,flush,total,metric_count };
constexpr size_t payload_size=8+8*4+metric_count*4+4;
constexpr size_t packet_size=7+payload_size;
struct Sample {
 uint8_t state=0,flags=0;
 uint32_t build_id=0,sequence=0,uptime_ms=0,dropped=0,voice_skipped=0;
 uint32_t previous_observer_us=0,cpu_mhz=0,period_us=33000;
 uint32_t us[metric_count]={};
};
inline uint32_t elapsed(uint32_t now,uint32_t before) { return now-before; }
inline void put32(uint8_t* output,uint32_t value) {
 for(int i=0;i<4;++i)output[i]=static_cast<uint8_t>(value>>(8*i));
}
inline uint32_t crc32(const uint8_t* bytes,size_t size) {
 uint32_t crc=0xffffffffu;
 for(size_t i=0;i<size;++i) {
  crc^=bytes[i];
  for(int bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320u:0);
 }
 return ~crc;
}
inline void encode(const Sample& s,uint8_t (&packet)[packet_size]) {
 const uint8_t header[]={'S','3','V','1','T',static_cast<uint8_t>(payload_size),0};
 for(size_t i=0;i<7;++i)packet[i]=header[i];
 uint8_t* data=packet+7;
 data[0]='S';data[1]='3';data[2]='P';data[3]='F';
 data[4]=1;data[5]=s.state;data[6]=s.flags;data[7]=0;
 const uint32_t words[]={s.build_id,s.sequence,s.uptime_ms,s.dropped,s.voice_skipped,
                         s.previous_observer_us,s.cpu_mhz,s.period_us};
 for(size_t i=0;i<8;++i)put32(data+8+i*4,words[i]);
 for(size_t i=0;i<metric_count;++i)put32(data+40+i*4,s.us[i]);
 put32(data+payload_size-4,crc32(data,payload_size-4));
}
static_assert(packet_size<=256,"Timing packet must fit the default HWCDC TX ring");
} // namespace frame_profile
