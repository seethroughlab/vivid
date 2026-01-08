# Song Arrangement Example

Demonstrates section-based composition for coordinated audio-visual changes.

## Operators Demonstrated

- **Song** - Define sections (intro, verse, chorus, etc.)
- **Sequencer** - Step-based pattern playback
- **Clock** - Master timing source

## Key Concepts

### Song Structure
Define named sections with bar ranges:
```cpp
auto& song = chain.add<Song>("song");
song.syncTo("clock");

// Section format: name, startBar, endBar (exclusive)
song.addSection("intro", 0, 8);      // Bars 0-7 (8 bars)
song.addSection("verse", 8, 24);     // Bars 8-23 (16 bars)
song.addSection("chorus", 24, 32);   // Bars 24-31 (8 bars)
song.addSection("outro", 56, 64);
```

### Querying Song State
```cpp
// Current section
const std::string& section = song.section();
int sectionIdx = song.sectionIndex();

// Progress (0-1)
float sectionProgress = song.sectionProgress();  // Through current section
float songProgress = song.songProgress();        // Through entire song

// Position
uint32_t bar = song.currentBar();
float beat = song.currentBeat();

// Edge detection
if (song.sectionJustStarted()) { /* new section began */ }
if (song.barJustStarted()) { /* new bar began */ }
```

### Section-Based Visuals
```cpp
void update(Context& ctx) {
    auto& song = chain.get<Song>("song");

    if (song.section() == "chorus") {
        // Intense visuals
        particles.emitRate = 500;
        bloom.intensity = 2.0f;
    } else if (song.section() == "verse") {
        // Moderate intensity
        particles.emitRate = 100;
        bloom.intensity = 0.8f;
    }

    // Smooth transitions within section
    float t = song.sectionProgress();
    filter.cutoff = 500 + t * 3500;  // Opens up through section
}
```

### Playback Control
```cpp
song.jumpToSection("chorus");    // Jump by name
song.jumpToBar(24);              // Jump to specific bar
song.nextSection();              // Advance to next section
song.previousSection();          // Go back
```

### Section Callbacks
```cpp
song.onSectionChange([](const std::string& prev, const std::string& next) {
    std::cout << "Transitioning from " << prev << " to " << next << std::endl;
});
```

## Clock Synchronization

Song reads timing from a Clock operator:
```cpp
auto& clock = chain.add<Clock>("clock");
clock.bpm = 120.0f;
clock.beatsPerBar = 4;

song.syncTo("clock");  // Connects by name
```

## Related Operators

- **Sequencer** - Step sequencing synced to Clock
- **Euclidean** - Algorithmic rhythm patterns
- **Clock** - Master tempo and bar tracking
