# Future Splash

Port of the Paper.js "Future Splash" demo. A dramatic wave simulation with spring physics that responds to movement.

## Vision

A bold black wave cuts across a white background, its surface rippling and responding to invisible forces. The wave is defined by spring-connected points that simulate tension and momentum, creating organic, fluid motion.

## Technical Approach

- Canvas API for filled path rendering
- Spring physics between adjacent points
- Fixed endpoints to anchor the wave
- Smooth quadratic curves through control points

## Physics

- 16 points across the screen width
- First and last 2 points are fixed (anchored)
- Springs maintain rest length between neighbors
- Mouse/animation pushes nearby points away
- Multiple spring iterations per frame for stability
- Friction dampens oscillations over time

## Visual Style

- High contrast: black wave on white background
- Filled shape from bottom of screen through wave curve
- Clean, minimal aesthetic inspired by Flash-era graphics
