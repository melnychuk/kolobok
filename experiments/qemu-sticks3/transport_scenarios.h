#pragma once
#include "transport_timing.h"
#include "measured_frame_envelopes.h"
#include <cstdio>

namespace stick_timing {
// Same deterministic bus regression scenarios execute natively and in QEMU.
// Most render costs are explicit assumptions. Separately labeled round-2/cache
// cases replay physical per-mode occupancy envelopes, not CPU cycle costs.
inline bool scenarios() {
    bool pass=true;
    auto check=[&](bool condition,const char* name) {
        if(!condition){printf("TRANSPORT_FAIL assertion=%s\n",name);pass=false;}
    };
    {
        Transport t;
        check(t.ok(),"profile");
        const uint64_t first=t.lcd_flush(),cached=t.lcd_flush();
        check(first==12963 && cached==12961,"lcd_40mhz_8bit_commands");
        check(t.lcd_flush(240*135,true)==12963,"window_cache_invalidated");
        printf("TRANSPORT_CASE name=lcd first_us=%llu cached_us=%llu\n",
               static_cast<unsigned long long>(first),static_cast<unsigned long long>(cached));
    }
    {
        Transport t;
        check(t.voice_block(),"voice_enqueue");
        const uint64_t blocked=t.stats().tx_block_us;
        t.advance(80000);
        check(t.stats().tx_delivered==2567,"voice_wire_bytes");
        check(t.stats().tx_packets==51 && t.stats().zlp_packets==0,"bytebuf_wrap_packetization");
        check(t.stats().tx_peak<=256 && blocked<80000,"bounded_voice_queue");
        printf("TRANSPORT_CASE name=voice host_slots=4 write_block_us=%llu packets=%llu zlp=%llu\n",
               static_cast<unsigned long long>(blocked),static_cast<unsigned long long>(t.stats().tx_packets),
               static_cast<unsigned long long>(t.stats().zlp_packets));
    }
    {
        Transport t;
        check(t.write(256)==256,"aligned_write");
        t.advance(8000);
        check(t.stats().tx_packets==4 && t.stats().zlp_packets==1,"aligned_zero_length_terminator");
    }
    for(uint32_t slots: {1u,4u,8u}) {
        Profile cfg;cfg.host_slots=slots;
        Transport t(cfg);
        for(uint64_t at=0;at<=6000000;at+=2000000)t.receive_at(at,8); // C|3|0|2\n fixture sizes
        for(int frame=0;frame<120;++frame)t.frame(16000,true);
        const auto& s=t.stats();
        check(s.short_writes==0 && s.rx_dropped==0,"normal_host_no_corruption");
        check(s.deadline_misses==0,"assumed_16ms_render_plus_lcd_budget");
        check(s.tx_peak<=256 && s.rx_peak<=256,"normal_queue_bounds");
        printf("TRANSPORT_CASE name=loop cpu_source=assumed_16000us host_slots=%u "
               "max_busy_us=%llu max_start_gap_us=%llu late_starts=%llu usb_block_us=%llu rx_max_poll_us=%llu\n",
               static_cast<unsigned>(slots),static_cast<unsigned long long>(s.max_frame_us),
               static_cast<unsigned long long>(s.max_start_gap_us),static_cast<unsigned long long>(s.late_starts),
               static_cast<unsigned long long>(s.tx_block_us),static_cast<unsigned long long>(s.rx_max_poll_us));
    }
    {
        Transport t;
        for(int frame=0;frame<30;++frame)t.frame(25000,false);
        check(t.stats().deadline_misses==30,"assumed_25ms_render_detects_over_budget");
        printf("TRANSPORT_CASE name=cpu_overload cpu_source=assumed_25000us busy_over_budget=%llu\n",
               static_cast<unsigned long long>(t.stats().deadline_misses));
    }
    {
        // Double buffering: an assumed 25ms render (the cached working/vertical
        // render-side median was ~22ms plus loop work) fits 33ms only when the
        // ~13ms transfer overlaps rendering. Assumed cost, not a measurement.
        Profile cfg;cfg.overlapped_flush=true;
        Transport t(cfg);
        for(int frame=0;frame<30;++frame)t.frame(25000,false);
        const auto& s=t.stats();
        check(s.deadline_misses==0 && s.late_starts==0,"overlapped_flush_25ms_render_fits");
        check(s.lcd_flushes==30 && s.flush_wait_us==0,"overlapped_flush_never_waits_at_25ms");
        Transport slow(cfg);
        for(int frame=0;frame<10;++frame)slow.frame(5000,false);
        check(slow.stats().flush_wait_us==0,"overlap_wait_only_when_dma_busy");
        // Faster than the wire: the next queue must wait for the previous DMA.
        Profile fast=cfg;fast.frame_period_us=10000;
        Transport tight(fast);
        for(int frame=0;frame<10;++frame)tight.frame(5000,false);
        check(tight.stats().flush_wait_us>0 && tight.stats().deadline_misses>0,
              "overlap_waits_for_previous_dma");
        printf("TRANSPORT_CASE name=overlapped_flush cpu_source=assumed_25000us busy_over_budget=%llu "
               "max_busy_us=%llu flush_wait_us=%llu\n",
               static_cast<unsigned long long>(s.deadline_misses),
               static_cast<unsigned long long>(s.max_frame_us),
               static_cast<unsigned long long>(s.flush_wait_us));
    }
    {
        // A render longer than the transfer period still cannot reach 30fps.
        Profile cfg;cfg.overlapped_flush=true;
        Transport t(cfg);
        for(int frame=0;frame<20;++frame)t.frame(36000,false);
        check(t.stats().deadline_misses==20,"overlap_does_not_hide_cpu_overload");
    }
    // Use explicit physical native-renderer envelopes only in these cases.
    // No recording: these captures contain no measured voice-load timing.
    for(const auto* measured: stick_measured::all) {
        const auto& envelope=*measured;
        Transport t;
        for(uint64_t at=0;at<=2000000;at+=2000000)t.receive_at(at,8);
        for(int frame=0;frame<40;++frame)t.frame(envelope.preWireP95Us,false);
        const auto& s=t.stats();
        check(s.deadline_misses==40,"physical_envelope_still_misses_33ms");
        check(s.max_frame_us==envelope.preWireP95Us+12963,"envelope_plus_source_wire");
        check(s.max_start_gap_us==envelope.preWireP95Us+12963+2000,
              "envelope_late_start_not_fictional_30fps");
        check(s.rx_dropped==0 && s.short_writes==0 && s.tx_accepted==0,
              "envelope_status_only_no_voice_assumption");
        printf("TRANSPORT_CASE name=physical_occupancy_replay build_id=%s optimization=%s "
               "mode=%s cpu_source=not_cycle_calibrated pre_wire_us=%u "
               "modeled_busy_us=%llu modeled_start_gap_us=%llu\n",
               envelope.buildId,envelope.optimization,envelope.mode,
               static_cast<unsigned>(envelope.preWireP95Us),
               static_cast<unsigned long long>(s.max_frame_us),
               static_cast<unsigned long long>(s.max_start_gap_us));
    }
    {
        Profile cfg;cfg.spi_hz=20000000;
        Transport t(cfg);
        t.frame(16000,false);
        check(t.stats().deadline_misses==1,"slower_spi_detects_over_budget");
    }
    {
        Transport t;
        t.pause_host(0,3000000,true); // IN-only backpressure; OUT/status remains live
        t.receive_at(0,8);
        check(!t.voice_block(),"host_stall_short_write_detected");
        check(t.now()==2000000 && t.stats().short_writes==1,"20_consecutive_100ms_timeouts");
        check(t.stats().tx_accepted==263,"header_plus_one_payload_chunk_before_stall");
        check(t.stats().rx_delivered==8,"out_endpoint_live_during_in_backpressure");
        printf("TRANSPORT_CASE name=host_stall expected_fault=short_write accepted=%llu blocked_us=%llu\n",
               static_cast<unsigned long long>(t.stats().tx_accepted),
               static_cast<unsigned long long>(t.stats().tx_block_us));
    }
    {
        Transport t;
        t.receive_at(0,320);
        t.lcd_flush(); // host/ISR can fill the RX queue while main blocks on SPI
        t.poll_rx();
        check(t.stats().rx_delivered==256 && t.stats().rx_dropped==64,"rx_overflow_detected");
        printf("TRANSPORT_CASE name=rx_burst expected_fault=overflow dropped=%llu\n",
               static_cast<unsigned long long>(t.stats().rx_dropped));
    }
    {
        Profile cfg;cfg.spi_hz=0;
        Transport t(cfg);
        check(!t.ok() && t.write(1)==0 && t.lcd_flush()==0,"invalid_profile_fails_closed");
    }
    printf("%s\n",pass?"TRANSPORT_PASS":"TRANSPORT_FAIL");
    return pass;
}
} // namespace stick_timing
