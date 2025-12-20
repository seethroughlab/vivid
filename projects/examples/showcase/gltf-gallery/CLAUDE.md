# GLTF Gallery

## Vision

Create an elegant 3D model viewer that showcases GLTF models with professional PBR rendering and image-based lighting. The gallery should feel like a high-end product visualization tool - smooth camera movement, beautiful lighting, and polished post-processing.

Models should be automatically framed and lit to look their best. The camera gently orbits to show all angles while the user can cycle through available models.

## Key Features

- Automatic discovery and loading of GLTF/GLB models
- PBR rendering with metallic-roughness workflow
- HDR environment map for realistic reflections (IBL)
- Automatic camera framing based on model bounds
- Smooth orbiting camera animation
- Post-processing: bloom, vignette, color grading

## Technical Approach

- Scan assets/models/ directory for .glb and .gltf files
- GLTFLoader with texture support and tangent computation
- IBLEnvironment for environment reflections
- Render3D with PBR shading and skybox
- CameraOperator with orbit animation
- Bloom, vignette, and HSV for post-processing

## Operators to Use

| Operator | Purpose |
|----------|---------|
| GLTFLoader | Load 3D models |
| SceneComposer | Scene setup |
| CameraOperator | Orbit camera with auto-framing |
| DirectionalLight | Key light |
| IBLEnvironment | HDR environment for reflections |
| Render3D | PBR rendering with skybox |
| Bloom | Highlight glow |
| CRTEffect | Vignette only |
| HSV | Color grading |

## Interaction

- **Space**: Cycle to next model
- **1-5**: Select specific model directly
- **B**: Toggle bloom effect

## Visual Style

- Dark, moody background
- Warm key light from upper right
- Cool fill from environment reflections
- Gentle auto-orbit reveals all angles
- Subtle bloom on bright surfaces
- Warm color grading
- Vignette for focus
