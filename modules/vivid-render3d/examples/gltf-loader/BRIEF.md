# Vision

A polished 3D model viewer that loads GLTF/GLB files and presents them with physically-based rendering, HDR environment lighting, and emissive bloom for a professional showcase experience.

## Aesthetic Goals
- Studio-quality lighting with image-based reflections from HDR skybox
- Cinematic bloom on emissive surfaces creates glowing highlights
- Auto-fitting camera ensures each model is perfectly framed

## Techniques Demonstrated
- GLTFLoader with texture loading and tangent computation for normal maps
- IBLEnvironment for HDR environment mapping and skybox display
- PBR rendering with metallic/roughness workflow
- ImGui controls for real-time adjustment of lighting and bloom parameters
- Dynamic model switching with automatic camera framing
