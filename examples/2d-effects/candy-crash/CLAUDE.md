# Candy Crash

Port of the classic Paper.js "Candy Crash" demo. Colorful bouncing balls that squish and deform when they collide.

## Vision

Recreate the playful physics-driven animation where soft-body blobs bounce around the screen. Each ball has a unique hue and deforms organically when it contacts other balls, creating a satisfying squishy feel.

## Technical Approach

- Canvas API for immediate-mode 2D drawing
- Custom Ball struct with soft-body physics (spring-based segment deformation)
- Quadratic bezier curves for smooth blob outlines
- HSL to RGB color conversion for vibrant randomized colors

## Features

- 18 balls with varying radii (60-120 pixels)
- Segment-based collision detection and response
- Smooth curve rendering through control points
- Screen wrapping at boundaries
