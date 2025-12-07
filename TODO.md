[x] I don't want "Vivid V3" in all of the headers. This isn't *really* v3 -- it's just that I had 2 false start. So really, this is vivid 0.1.0.
[x] the noise operator should support many versions of noise: perlin, simplex, etc.
[x] Add to the chain-demo: a noise node DISTORTS a texture loaded from assets/images
[x] The title bar should include the name of the project
[x] The 'f' key should toggle fullscreen 
[x] I don't like the extern "C" stuff in the chain.cpp files. It seems like boilerplate. In vivid_v1 we just had a VIVID_CHAIN(setup, update) macro. Is it possible to use that approach here?
[x] Create a README.md that follows the conventions of READMEs on github
[x] Add reminders to the ROADMAP to update the readme at the end of major phases.
[x] PLAN: we just finished phase 4. what kind of examples would best illustrate (and test) everyting we added? 
[x] It seems that, in the jump to v3, we lost some of the emphasis on vivid bring LLM-first in the documentation. Let's update the README and the ROADMAP so that they reflect that one of the core philosophies of the project is to be LLM-friendly. It's already mentioned in docs/PHILOSOPHY.md in the "LLM-Native Workflow" section.
[x] Is everything we've done so far likely to work on Windows when I test? vivid_v1 was fully croass platform, so maybe it's a good idea to see how it was handled there.
    - Added Windows Media Foundation decoder (mf_decoder.cpp) ported from vivid_v1
    - Added Linux FFmpeg decoder stub (ffmpeg_decoder.cpp)
    - Updated CMakeLists.txt with platform-specific source selection
    - Updated hot-reload to link video addon on Windows
    - HAP codec requires FFmpeg on Windows/Linux (standard codecs work via MF/FFmpeg)
[x] The hot-reloader shouldn't have hard-coded addon paths. It should be dynamic becuase users might add more addons. Will that be changed when we create the addon registry? If so, is that change explicitly mentioned in the ROADMAP?
    - Yes, planned for Phase 16a (Addon Registry) - now explicitly added to ROADMAP
    - Will scan `#include` directives or read vivid.json to discover addon dependencies
[x] ALL platforms should use the official Vidvox code (hap.h, hap.cpp) to decode hap videos. It's highly optimized. Is that how it's done now?
    - **macOS (v3)**: ✅ Yes - AVFoundation demux → Vidvox hap.c → direct DXT texture upload (optimal, ported from v2)
    - **Windows/Linux (v3)**: ❌ No - HAP is a stub, falls back to standard codec decoder
    - **vivid_v2 design (correct)**: FFmpeg demux + Vidvox hap.c → DXT upload (planned for all platforms, only macOS implemented)
    - **vivid_v1 (incorrect)**: Used FFmpeg's built-in HAP decoder which converts to RGBA, losing DXT benefit
    - **TODO**: For Windows/Linux HAP support, use FFmpeg for container demux only (libavformat), then Vidvox hap.c for DXT decompression, then upload BC1/BC3 compressed texture to GPU
[x] At some point, the video player was working fine. but now it looks like some channels are mixed up. blue is green.
    - Fixed AVFDecoder: Changed from BGRA8Unorm to RGBA8Unorm with explicit BGRA→RGBA channel swap during pixel copy
    - Fixed HSV effect: Replaced buggy compact rgb2hsv/hsv2rgb functions with clearer standard implementations
[x] Why are you setting VK_ICD_FILENAMES, VK_DRIVER_FILES, and DYLD_LIBRARY_PATH when you launch examples. We don't need those anymore, right?
[x] When you run a vivid program, the imgui.ini file should be saved in the local folder, not in the root folder
[x] What changes can we make to make vivid even more LLM-friendly? Should we create better documentation they would make it easier for an LLM to quickly understand vivid? Should we enforce better user workflow, such as roadmaps and plans? Should we use other well known programing patterns like Fluent? Or something else completely?
    - Created docs/LLM-REFERENCE.md - compact operator reference for LLMs
    - Created docs/RECIPES.md - complete chain.cpp examples for common effects
    - Created examples/template/ - well-commented starter project with CLAUDE.md
    - Updated README.md with LLM documentation section and CLAUDE.md guidance
[x] Set up Doxygen for autodoc documentation
    - Created Doxyfile with v3 paths
    - Added Doxygen-style comments to core headers (operator.h, context.h, chain.h, param.h)
    - Added Doxygen-style comments to key operator headers (noise.h, output.h, composite.h, texture_operator.h)
    - Added `make docs` target to CMakeLists.txt (requires Doxygen to be installed)