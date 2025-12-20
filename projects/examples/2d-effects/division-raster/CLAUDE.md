# Division Raster

Port of the Paper.js "Division Raster" demo. Progressive image reveal through recursive subdivision, with each rectangle filled by the average color of that image region.

## Vision

Create a hypnotic mosaic effect where an image is revealed through animated subdivision. Starting from a single rectangle, the algorithm progressively divides the largest cells, filling each with the average color sampled from the source image. The result is a dynamic pixelation effect that gradually resolves into recognizable imagery.

## Technical Approach

- Canvas API for rectangle rendering
- Image operator with `keepCpuData` for pixel sampling
- `getAverageColor()` to sample regions from source image
- Greedy subdivision: always split the largest dividable rectangle

## Animation

- 3 subdivisions per frame, up to 500 total rectangles
- Landscape rectangles split horizontally, portrait split vertically
- Subtle borders on larger cells for visibility
