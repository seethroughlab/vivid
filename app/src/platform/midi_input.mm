#include "platform/midi_input.h"

#import <CoreMIDI/CoreMIDI.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace vivid::platform {

namespace {

std::string cf_to_std(CFStringRef s) {
    if (!s) return {};
    char buf[256];
    if (CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8)) return std::string(buf);
    return {};
}

// "Manufacturer Model", falling back to whatever CoreMIDI will give us. Endpoint names alone are
// often just "Port 1", which is useless in a device picker.
std::string endpoint_name(MIDIEndpointRef ep) {
    CFStringRef cf = nullptr;
    std::string manuf, model, name;
    if (MIDIObjectGetStringProperty(ep, kMIDIPropertyManufacturer, &cf) == noErr && cf) { manuf = cf_to_std(cf); CFRelease(cf); cf = nullptr; }
    if (MIDIObjectGetStringProperty(ep, kMIDIPropertyModel, &cf) == noErr && cf)        { model = cf_to_std(cf); CFRelease(cf); cf = nullptr; }
    if (MIDIObjectGetStringProperty(ep, kMIDIPropertyDisplayName, &cf) == noErr && cf)  { name  = cf_to_std(cf); CFRelease(cf); cf = nullptr; }
    if (!manuf.empty() && !model.empty()) {
        // Don't produce "Arturia Arturia KeyLab" when the model already carries the maker.
        if (model.rfind(manuf, 0) == 0) return model;
        return manuf + " " + model;
    }
    if (!name.empty())  return name;
    if (!model.empty()) return model;
    if (MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &cf) == noErr && cf) { name = cf_to_std(cf); CFRelease(cf); }
    return name.empty() ? std::string("MIDI source") : name;
}

int32_t endpoint_uid(MIDIEndpointRef ep) {
    SInt32 uid = 0;
    if (MIDIObjectGetIntegerProperty(ep, kMIDIPropertyUniqueID, &uid) != noErr) return 0;
    return int32_t(uid);
}

struct Impl {
    MIDIClientRef client = 0;
    MIDIPortRef   port   = 0;

    static constexpr int kCap = 1024;                  // ring of decoded messages
    MidiMsg               ring[kCap];
    std::atomic<uint32_t> head{0};                     // producer: the CoreMIDI read thread
    std::atomic<uint32_t> tail{0};                     // consumer: the UI thread (poll)

    // One parser PER SOURCE: running status and a partial message are per-cable state, so sharing
    // one parser across devices would let a half-received message from one merge with bytes from
    // another. `connRefCon` carries the slot index.
    //
    // A FIXED array, never a vector: the read thread indexes this concurrently with a rescan on the
    // main thread, and a vector would reallocate under it. Slots are only ever reused after the
    // source is disconnected, and `parser_count` is published with release/acquire so the read
    // thread never sees a slot before it is initialised.
    static constexpr int kMaxSources = 32;
    struct Src { int32_t uid = 0; vivid::session::MidiByteParser parser; };
    Src                   parsers[kMaxSources];
    std::atomic<int>      parser_count{0};
    mutable std::mutex        list_mtx;                // guards `list` (UI reads, notify writes)
    std::vector<MidiSource>   list;

    std::atomic<int32_t>  want_source{0};              // 0 = any
    std::atomic<int>      want_channel{-1};            // -1 = any
    std::atomic<int>      connected{0};
    std::atomic<uint64_t> events{0};
    std::atomic<uint64_t> last_time{0};

    void push(const MidiMsg& m) {
        const uint32_t h = head.load(std::memory_order_relaxed);
        const uint32_t n = (h + 1) % kCap;
        if (n == tail.load(std::memory_order_acquire)) return;   // full — drop
        ring[h] = m;
        head.store(n, std::memory_order_release);
    }
};

// CoreMIDI delivers on its own thread. Decode and push; no allocation, no locks.
void read_proc(const MIDIPacketList* pl, void* refCon, void* srcConn) {
    auto* impl = static_cast<Impl*>(refCon);
    const size_t si = reinterpret_cast<uintptr_t>(srcConn);
    if (si >= size_t(impl->parser_count.load(std::memory_order_acquire))) return;
    auto& parser = impl->parsers[si].parser;
    const int want_ch = impl->want_channel.load(std::memory_order_relaxed);

    const MIDIPacket* p = &pl->packet[0];
    for (UInt32 i = 0; i < pl->numPackets; ++i) {
        parser.feed(p->data, p->length, uint64_t(p->timeStamp), [&](const MidiMsg& m) {
            if (want_ch >= 0 && m.channel != want_ch) return;
            impl->events.fetch_add(1, std::memory_order_relaxed);
            impl->last_time.store(m.host_time, std::memory_order_relaxed);
            impl->push(m);
        });
        p = MIDIPacketNext(p);
    }
}

// Disconnect everything, re-enumerate, reconnect what the selection allows. Runs on the thread
// CoreMIDI calls the notify proc on (the thread that created the client — our main thread) and at
// start(). The read thread is quiesced by MIDIPortDisconnectSource before `parsers` is rebuilt.
void rescan(Impl* impl) {
    // Drop every existing connection FIRST, and park the count at 0, so the read thread bails out
    // of any in-flight callback rather than indexing a slot we are about to rewrite.
    impl->parser_count.store(0, std::memory_order_release);
    const ItemCount ns = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < ns; ++i)
        if (MIDIEndpointRef ep = MIDIGetSource(i)) MIDIPortDisconnectSource(impl->port, ep);

    std::vector<MidiSource> list;
    const int32_t want = impl->want_source.load(std::memory_order_relaxed);
    int slots = 0, connected = 0;
    for (ItemCount i = 0; i < ns; ++i) {
        MIDIEndpointRef ep = MIDIGetSource(i);
        if (!ep) continue;
        MidiSource s;
        s.id   = endpoint_uid(ep);
        s.name = endpoint_name(ep);
        s.connected = (want == 0 || want == s.id) && slots < Impl::kMaxSources;
        if (s.connected) {
            const size_t idx = size_t(slots);
            impl->parsers[idx].uid = s.id;
            impl->parsers[idx].parser.reset();   // a fresh cable starts with no running status
            ++slots;
            if (MIDIPortConnectSource(impl->port, ep, reinterpret_cast<void*>(uintptr_t(idx))) == noErr)
                ++connected;
            else
                { s.connected = false; --slots; }
        }
        list.push_back(std::move(s));
    }
    impl->parser_count.store(slots, std::memory_order_release);   // publish AFTER the slots are init'd
    impl->connected.store(connected, std::memory_order_relaxed);
    { std::lock_guard<std::mutex> lk(impl->list_mtx); impl->list.swap(list); }
}

void notify_proc(const MIDINotification* msg, void* refCon) {
    if (!msg) return;
    // A device appeared, went away, or the setup otherwise changed — re-enumerate. This is the
    // whole of hot-plug support; without a notify proc a keyboard plugged in after launch was
    // invisible until the app restarted.
    if (msg->messageID == kMIDIMsgSetupChanged || msg->messageID == kMIDIMsgObjectAdded ||
        msg->messageID == kMIDIMsgObjectRemoved)
        rescan(static_cast<Impl*>(refCon));
}

}  // namespace

MidiInput::~MidiInput() { stop(); }

bool MidiInput::start() {
    if (impl_) return true;
    auto* impl = new Impl();
    if (MIDIClientCreate(CFSTR("Vivid"), notify_proc, impl, &impl->client) != noErr) { delete impl; return false; }
    if (MIDIInputPortCreate(impl->client, CFSTR("Vivid In"), read_proc, impl, &impl->port) != noErr) {
        MIDIClientDispose(impl->client); delete impl; return false;
    }
    impl_ = impl;
    rescan(impl);
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

int MidiInput::poll(MidiMsg* out, int max) {
    if (!impl_ || !out || max <= 0) return 0;
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
    return impl_ ? static_cast<Impl*>(impl_)->connected.load(std::memory_order_relaxed) : 0;
}

std::vector<MidiSource> MidiInput::sources() const {
    if (!impl_) return {};
    auto* impl = static_cast<Impl*>(impl_);
    std::lock_guard<std::mutex> lk(impl->list_mtx);
    return impl->list;
}

void MidiInput::select(int32_t source_id, int channel) {
    if (!impl_) return;
    auto* impl = static_cast<Impl*>(impl_);
    impl->want_source.store(source_id, std::memory_order_relaxed);
    impl->want_channel.store(channel, std::memory_order_relaxed);
    rescan(impl);   // re-apply the filter to the live connections
}

int32_t MidiInput::selected_source() const {
    return impl_ ? static_cast<Impl*>(impl_)->want_source.load(std::memory_order_relaxed) : 0;
}
int MidiInput::selected_channel() const {
    return impl_ ? static_cast<Impl*>(impl_)->want_channel.load(std::memory_order_relaxed) : -1;
}
uint64_t MidiInput::events_seen() const {
    return impl_ ? static_cast<Impl*>(impl_)->events.load(std::memory_order_relaxed) : 0;
}
uint64_t MidiInput::last_event_host_time() const {
    return impl_ ? static_cast<Impl*>(impl_)->last_time.load(std::memory_order_relaxed) : 0;
}

}  // namespace vivid::platform
