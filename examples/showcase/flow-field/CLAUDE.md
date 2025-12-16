# Flow Field

## Vision

Create a mesmerizing generative art piece with thousands of particles flowing through invisible force fields. The aesthetic should feel like digital nature - organic movement patterns emerging from simple rules. Think wind visualization, aurora borealis, or bioluminescent ocean currents.

The piece should be hypnotic and meditative, with particles that feel alive and responsive. Multiple layers of movement at different speeds create visual depth. A subtle network of connected nodes adds geometric structure to the organic flow.

## Key Features

- Multiple particle layers with different behaviors (slow flowing, swirling around attractors, fast accents)
- GPU-based plexus network connecting nearby particles with glowing lines
- Feedback trails for motion blur and persistence
- Multiple color themes to match different moods
- Real-time parameter control via mouse and keyboard

## Technical Approach

- 3 Particles operators with different emitter shapes (Disc, Ring)
- Plexus operator for GPU-computed line network between nodes
- Composite with Add blend mode to layer everything
- Feedback for trail persistence with subtle zoom/rotate
- Bloom for ethereal glow effect

## Operators to Use

| Operator | Purpose |
|----------|---------|
| Particles (x3) | Three layers: slow flow, swirling attractors, fast accents |
| Plexus | GPU network of connected nodes |
| Composite | Layer particles and plexus together |
| Feedback | Trail persistence with zoom/rotate |
| Bloom | Soft glow effect |

## Interaction

- **Keys 1-4**: Switch between color themes (Cyber, Matrix, Ember, Void)
- **Space**: Reset all particles
- **R**: Randomize attractor positions
- **Mouse X**: Turbulence intensity
- **Mouse Y**: Trail length (feedback decay)

## Visual Style

- Dark background with bright, glowing particles
- Organic flowing movement with subtle geometric structure
- Color themes:
  - **Cyber**: Cyan and blue tones
  - **Matrix**: Green digital aesthetic
  - **Ember**: Warm orange and red
  - **Void**: Cool grays and purples
- Soft bloom glow on all elements
- Persistent trails that slowly fade
