#include "platform/midi_input.h"

#import <CoreMIDI/CoreMIDI.h>
#include <atomic>
#include <cstdint>

namespace vivid::platform {

namespace {
struct Impl {
    MIDIClientRef client = 0;
    MIDIPortRef   port   = 0;
    int           sources = 0;
    static constexpr int kCap = 1024;         // ring of note transitions
    MidiEvent             ring[kCap];
    std::atomic<uint32_t> head{0};            // producer: the CoreMIDI read thread
    std::atomic<uint32_t> tail{0};            // consumer: the UI thread (poll)

    void push(const MidiEvent& e) {
        const uint32_t h = head.load(std::memory_order_relaxed);
        const uint32_t n = (h + 1) % kCap;
        if (n == tail.load(std::memory_order_acquire)) return;   // full — drop
        ring[h] = e;
        head.store(n, std::memory_order_release);
    }
};

// CoreMIDI delivers on its own thread; parse note on/off and push transitions.
void read_proc(const MIDIPacketList* pl, void* refCon, void* /*srcConn*/) {
    auto* impl = static_cast<Impl*>(refCon);
    const MIDIPacket* p = &pl->packet[0];
    for (UInt32 i = 0; i < pl->numPackets; ++i) {
        const UInt16 n = p->length;
        const Byte*  d = p->data;
        UInt16 j = 0;
        while (j < n) {
            const Byte status = d[j];
            if (status < 0x80) { ++j; continue; }   // data byte w/o status (running status unsupported)
            const Byte type = status & 0xF0;
            if ((type == 0x90 || type == 0x80) && j + 2 < n) {   // note on / note off (need 2 data bytes)
                const Byte pitch = d[j + 1] & 0x7F;
                const Byte vel   = d[j + 2] & 0x7F;
                const bool on    = (type == 0x90 && vel > 0);    // note-on vel 0 == note-off
                impl->push(MidiEvent{ on, static_cast<int>(pitch), vel / 127.0f });
                j += 3;
            } else {
                j += (type == 0xC0 || type == 0xD0) ? 2 : 3;     // program/aftertouch = 2, else 3
            }
        }
        p = MIDIPacketNext(p);
    }
}
}  // namespace

MidiInput::~MidiInput() { stop(); }

bool MidiInput::start() {
    if (impl_) return true;
    auto* impl = new Impl();
    if (MIDIClientCreate(CFSTR("Vivid"), nullptr, nullptr, &impl->client) != noErr) { delete impl; return false; }
    if (MIDIInputPortCreate(impl->client, CFSTR("Vivid In"), read_proc, impl, &impl->port) != noErr) {
        MIDIClientDispose(impl->client); delete impl; return false;
    }
    const ItemCount ns = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < ns; ++i)
        if (MIDIEndpointRef s = MIDIGetSource(i)) MIDIPortConnectSource(impl->port, s, nullptr);
    impl->sources = static_cast<int>(ns);
    impl_ = impl;
    return true;
}

void MidiInput::stop() {
    if (!impl_) return;
    auto* impl = static_cast<Impl*>(impl_);
    if (impl->port)   MIDIPortDispose(impl->port);
    if (impl->client) MIDIClientDispose(impl->client);
    delete impl;
    impl_ = nullptr;
}

int MidiInput::poll(MidiEvent* out, int max) {
    if (!impl_ || max <= 0) return 0;
    auto* impl = static_cast<Impl*>(impl_);
    int c = 0;
    while (c < max) {
        const uint32_t t = impl->tail.load(std::memory_order_relaxed);
        if (t == impl->head.load(std::memory_order_acquire)) break;
        out[c++] = impl->ring[t];
        impl->tail.store((t + 1) % Impl::kCap, std::memory_order_release);
    }
    return c;
}

int MidiInput::source_count() const {
    return impl_ ? static_cast<Impl*>(impl_)->sources : 0;
}

}  // namespace vivid::platform
