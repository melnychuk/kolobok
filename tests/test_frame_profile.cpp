#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../firmware/frame_profile.h"
#include "../experiments/qemu-sticks3/measured_frame_envelopes.h"

int main(int argc,char** argv) {
 using namespace frame_profile;
 const uint8_t known[]={'1','2','3','4','5','6','7','8','9'};
 assert(crc32(known,sizeof(known))==0xcbf43926u);
 assert(elapsed(20,0xfffffff0u)==36);
 assert(payload_size==88&&packet_size==95);
 Sample sample;
 sample.state=3;sample.flags=1;sample.build_id=0x12345678;
 sample.sequence=9;sample.uptime_ms=300;sample.cpu_mhz=240;sample.previous_observer_us=15;
 const uint32_t values[]={33000,1000,500,4000,1000,2500,500,0,1500,13000,24000};
 for(int i=0;i<metric_count;++i)sample.us[i]=values[i];
 uint8_t packet[packet_size];encode(sample,packet);
 assert(memcmp(packet,"S3V1T",5)==0&&packet[5]==payload_size&&packet[6]==0);
 assert(memcmp(packet+7,"S3PF",4)==0&&packet[11]==1);
 assert(packet[15]==0x78&&packet[16]==0x56&&packet[17]==0x34&&packet[18]==0x12);
 uint8_t expected[4];put32(expected,crc32(packet+7,payload_size-4));
 assert(memcmp(expected,packet+packet_size-4,4)==0);
 if(argc==2&&strcmp(argv[1],"--fixture")==0) {
  return fwrite(packet,1,sizeof(packet),stdout)==sizeof(packet)?0:1;
 }
 if(argc==2&&strcmp(argv[1],"--listening-fixture")==0) {
  // Mode/framing regression only: unchanged synthetic costs, no measured
  // occupancy envelope exists for the new authored listening performances.
  sample.state=6;sample.flags=1;sample.build_id=0x12345679;
  encode(sample,packet);
  return fwrite(packet,1,sizeof(packet),stdout)==sizeof(packet)?0:1;
 }
 // Prepared eye-pass binary has no physical capture/envelope. Placeholder
 // packet costs exercise source identity/framing, never predict its device FPS.
 assert(!stick_measured::findEnvelope(stick_measured::unmeasuredEyeSource,
          stick_measured::unmeasuredEyeBinary,"O2","working/flags-1"));
 if(argc==2&&strcmp(argv[1],"--eye-pass-fixture")==0) {
  sample.build_id=stick_measured::unmeasuredEyeBuildId;encode(sample,packet);
  return fwrite(packet,1,sizeof(packet),stdout)==sizeof(packet)?0:1;
 }
 assert(strcmp(stick_measured::round2[0].sourceSha256,stick_measured::round2[1].sourceSha256)==0);
 assert(strcmp(stick_measured::round2[0].buildId,stick_measured::round2[1].buildId)!=0);
 for(const auto* measured:stick_measured::all) {
  const auto& envelope=*measured;
  assert(strlen(envelope.buildId)==8 && strlen(envelope.sourceSha256)==64);
  assert(strlen(envelope.binarySha256)==64 && envelope.frames>=300);
  assert(envelope.preWireP95Us>envelope.renderP95Us && envelope.renderP95Us>0);
  assert(envelope.preWireP95Us+12960>33000); // Even the latest full pipeline misses.
  using stick_measured::findEnvelope;
  assert(findEnvelope(envelope.sourceSha256,envelope.binarySha256,
                      envelope.optimization,envelope.mode)==&envelope);
  assert(!findEnvelope("unmeasured-source",envelope.binarySha256,envelope.optimization,envelope.mode));
  assert(!findEnvelope(stick_measured::unmeasuredEyeSource,envelope.binarySha256,
                      envelope.optimization,envelope.mode));
  assert(!findEnvelope(envelope.sourceSha256,"unmeasured-binary",envelope.optimization,envelope.mode));
  assert(!findEnvelope(envelope.sourceSha256,envelope.binarySha256,"O3",envelope.mode));
  assert(!findEnvelope(envelope.sourceSha256,envelope.binarySha256,envelope.optimization,"working/flags-2"));
  assert(!findEnvelope(nullptr,envelope.binarySha256,envelope.optimization,envelope.mode));
  const bool historical=measured==&stick_measured::round2[0] || measured==&stick_measured::round2[1];
  if(argc==2 && ((strcmp(argv[1],"--occupancy-fixtures")==0 && historical) ||
                (strcmp(argv[1],"--cache-fixtures")==0 && !historical))) {
   // Preset transport fixture from published per-mode quantiles, NOT a real
   // frame or independent measurements of QEMU stages. Put render occupancy
   // in update solely to keep the packet's stage sum valid.
   Sample fixture;fixture.state=3;fixture.flags=1;fixture.cpu_mhz=240;
   fixture.build_id=static_cast<uint32_t>(strtoul(envelope.buildId,nullptr,16));
   fixture.us[interval]=envelope.startP95Us;
   fixture.us[update]=envelope.renderP95Us;fixture.us[flush]=envelope.flushP95Us;
   fixture.us[total]=envelope.renderP95Us+envelope.flushP95Us;
   encode(fixture,packet);
   if(fwrite(packet,1,sizeof(packet),stdout)!=sizeof(packet))return 1;
  }
 }
}
