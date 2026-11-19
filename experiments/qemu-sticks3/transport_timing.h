#pragma once
// Source-derived bus constraints plus an explicit, synthetic USB host schedule.
// No MMIO/device access. This is NOT a cycle-accurate USB/SPI peripheral model.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace stick_timing {
struct Profile {
    uint32_t spi_hz = 40000000;
    uint32_t usb_frame_us = 1000;
    uint32_t host_slots = 4; // assumption: combined IN/OUT grants per USB frame
    uint32_t tx_capacity = 256; // HWCDC BYTEBUF: no per-item header overhead
    uint32_t rx_capacity = 256;
    uint32_t endpoint_bytes = 64;
    uint32_t no_progress_us = 100000 * 20; // 20 consecutive 100ms waits
    uint32_t frame_period_us = 33000;
    uint32_t loop_delay_us = 2000;
    // Double-buffered firmware queues the frame by DMA and renders the next one
    // while it transfers; only the wait for the PREVIOUS transfer blocks. Off by
    // default: historical envelopes were captured with synchronous flushes.
    bool overlapped_flush = false;
};
struct Stats {
    uint64_t lcd_wire_us = 0, lcd_bytes = 0, lcd_flushes = 0;
    uint64_t tx_accepted = 0, tx_delivered = 0, tx_packets = 0, zlp_packets = 0;
    uint64_t tx_block_us = 0, short_writes = 0, rx_delivered = 0, rx_dropped = 0;
    uint64_t rx_max_poll_us = 0, max_frame_us = 0, deadline_misses = 0;
    uint64_t max_start_gap_us = 0, late_starts = 0, flush_wait_us = 0;
    uint32_t tx_peak = 0, rx_peak = 0;
};
struct Arrival { uint64_t at; size_t bytes; };
struct Pause { uint64_t start, end; bool in_only; };

class Transport {
public:
    explicit Transport(Profile profile = {}) : cfg(profile) {
        // <=8 grants/frame is a deliberately conservative test-profile range,
        // not a statement of USB's maximum throughput or guaranteed bandwidth.
        // 125us/grant leaves room for a full-speed 64B bulk transaction.
        if (!cfg.spi_hz || cfg.usb_frame_us != 1000 || cfg.host_slots < 1 || cfg.host_slots > 8 ||
            cfg.tx_capacity < cfg.endpoint_bytes || cfg.tx_capacity > 4096 ||
            !cfg.rx_capacity || cfg.rx_capacity > 4096 || cfg.endpoint_bytes != 64 ||
            !cfg.no_progress_us || !cfg.frame_period_us || !cfg.loop_delay_us) {
            valid = false;
        }
        next_slot = slot_time(1);
    }
    bool ok() const { return valid; }
    uint64_t now() const { return time_us_; }
    const Stats& stats() const { return counters; }
    void receive_at(uint64_t at, size_t bytes) {
        if (at < time_us_ || (!arrivals.empty() && at < arrivals.back().at)) { valid=false; return; }
        arrivals.push_back({at,bytes});
    }
    void pause_host(uint64_t start, uint64_t end, bool in_only=false) {
        if (start < time_us_ || end < start) { valid=false; return; }
        pauses.push_back({start,end,in_only});
    }
    void advance(uint64_t us) { advance_to(time_us_ + us); }
    // Main-loop parser drains the software queue; ISR/host progress continues
    // while advance()/LCD/CPU work blocks that parser.
    void poll_rx() {
        if (rx_size) counters.rx_max_poll_us = std::max(counters.rx_max_poll_us, time_us_-rx_first);
        rx_size=0;
    }
    size_t write(size_t bytes) {
        if (!valid) return 0;
        const uint64_t started=time_us_;
        size_t accepted=0;
        while (accepted < bytes) {
            const auto chunk=static_cast<uint32_t>(std::min<size_t>(cfg.tx_capacity,bytes-accepted));
            const uint64_t deadline=time_us_+cfg.no_progress_us;
            while (ring+chunk > cfg.tx_capacity && time_us_<deadline) {
                advance_to(std::min(next_slot,deadline));
            }
            if (ring+chunk > cfg.tx_capacity) { ++counters.short_writes; break; }
            ring+=chunk;
            counters.tx_peak=std::max(counters.tx_peak,ring);
            accepted+=chunk;
            counters.tx_accepted+=chunk;
            load_fifo();
        }
        counters.tx_block_us+=time_us_-started;
        return accepted;
    }
    bool voice_block() {
        // Firmware does two Serial.write calls: seven-byte S3V1 header, then
        // 1280 PCM16 samples. Counts only; no recording or transcript data.
        return write(7)==7 && write(2560)==2560;
    }
    uint64_t lcd_flush(uint32_t pixels=240*135, bool window_changed=false) {
        if (!valid) return 0;
        // CASET + 4 data, RASET + 4 data, RAMWR. Cached full-screen windows
        // omit CASET/RASET on later frames. GPIO D/C means eight, not nine bits.
        const uint64_t commands=(!window_cached || window_changed)?11:1;
        window_cached=true;
        const uint64_t bytes=static_cast<uint64_t>(pixels)*2+commands;
        const uint64_t duration=(bytes*8*1000000+cfg.spi_hz-1)/cfg.spi_hz;
        counters.lcd_wire_us+=duration;
        counters.lcd_bytes+=bytes;
        ++counters.lcd_flushes;
        if (cfg.overlapped_flush) {
            // Wait for the previous frame's DMA, then return while this one runs.
            if (dma_done>time_us_) { counters.flush_wait_us+=dma_done-time_us_; advance_to(dma_done); }
            dma_done=time_us_+duration;
            return duration;
        }
        advance(duration); // source pushSprite/end_transaction waits for SPI
        return duration;
    }
    // Schedule a representative main loop. CPU/convert time is supplied by the
    // caller (synthetic scenarios or explicitly uncalibrated QEMU time).
    void frame(uint64_t cpu_us, bool recording=true) {
        if (!valid) return;
        service_main(recording);
        const uint64_t start=time_us_;
        if (have_start) {
            const uint64_t gap=start-last_start;
            counters.max_start_gap_us=std::max(counters.max_start_gap_us,gap);
            if(gap>cfg.frame_period_us+cfg.loop_delay_us)++counters.late_starts;
        }
        have_start=true;
        last_start=start;
        advance(cpu_us);
        lcd_flush();
        const uint64_t busy=time_us_-start;
        counters.max_frame_us=std::max(counters.max_frame_us,busy);
        if (busy>cfg.frame_period_us) ++counters.deadline_misses;
        const uint64_t next=start+cfg.frame_period_us;
        // Firmware polls between render calls, at ~2ms cadence. A blocking
        // voice write can delay the next render rather than being free async IO.
        do { advance(cfg.loop_delay_us); service_main(recording); } while(time_us_<next);
    }
private:
    Profile cfg;
    Stats counters;
    bool valid=true, window_cached=false, zlp_pending=false, have_start=false;
    uint64_t time_us_=0, slot_index=1, next_slot=0, rx_first=0, next_audio=80000, last_start=0, dma_done=0;
    uint32_t ring=0, fifo=0, rx_size=0, ring_read=0;
    size_t pending_rx=0, arrival_index=0;
    std::vector<Arrival> arrivals;
    std::vector<Pause> pauses;
    uint64_t slot_time(uint64_t index) const {
        return index * cfg.usb_frame_us / std::max<uint32_t>(1,cfg.host_slots);
    }
    bool host_paused(uint64_t at, bool input_endpoint) const {
        for(const auto& pause:pauses)
            if(at>=pause.start && at<pause.end && (!pause.in_only || input_endpoint))return true;
        return false;
    }
    void load_fifo() {
        if(!fifo && ring) {
            // HWCDC ReceiveUpToFromISR cannot cross the BYTEBUF wrap boundary.
            // A seven-byte header shifts alignment and can create short packets.
            fifo=std::min(std::min(ring,cfg.endpoint_bytes),cfg.tx_capacity-ring_read);
            ring_read=(ring_read+fifo)%cfg.tx_capacity;
            ring-=fifo;
            zlp_pending=false;
        }
    }
    void grant() {
        // OUT and IN share a host-grant budget; neither has guaranteed service.
        if(pending_rx && !host_paused(time_us_,false)) {
            const auto packet=static_cast<uint32_t>(std::min<size_t>(pending_rx,cfg.endpoint_bytes));
            pending_rx-=packet;
            const uint32_t copied=std::min(packet,cfg.rx_capacity-rx_size);
            if(copied && !rx_size)rx_first=time_us_;
            rx_size+=copied;
            counters.rx_delivered+=copied;
            counters.rx_dropped+=packet-copied; // HWCDC ISR discards excess bytes
            counters.rx_peak=std::max(counters.rx_peak,rx_size);
        } else if(fifo && !host_paused(time_us_,true)) {
            const uint32_t packet=fifo;
            fifo=0;
            counters.tx_delivered+=packet;
            ++counters.tx_packets;
            zlp_pending=packet==cfg.endpoint_bytes && !ring;
            load_fifo();
        } else if(zlp_pending && !host_paused(time_us_,true)) {
            ++counters.zlp_packets;
            zlp_pending=false;
        }
    }
    void advance_to(uint64_t until) {
        if(!valid || until<time_us_)return;
        while(next_slot<=until) {
            time_us_=next_slot;
            while(arrival_index<arrivals.size() && arrivals[arrival_index].at<=time_us_) {
                pending_rx+=arrivals[arrival_index++].bytes;
            }
            grant();
            next_slot=slot_time(++slot_index);
        }
        time_us_=until;
    }
    void service_main(bool recording) {
        poll_rx();
        if(recording && time_us_>=next_audio) {
            voice_block();
            // Source rearms record() after its synchronous send. This captures
            // potential rearm gaps, not a full I2S/audio-driver simulation.
            next_audio=time_us_+80000;
        }
    }
};
} // namespace stick_timing
