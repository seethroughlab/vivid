# MCP Simplification Plan

## Goal

Remove `describe()` and `OperatorDescriptor` entirely. Replace with simple REGISTER macros + Doxygen comments.

## Current State

| Field | Source | Used By | After Simplification |
|-------|--------|---------|---------------------|
| `name` | describe() | list_operators, get_operator | REGISTER macro arg |
| `category` | describe() | list_operators, get_operator | REGISTER macro arg |
| `description` | describe() | get_operator | REGISTER macro arg |
| `requiresInput` | describe() | list_operators, get_operator | REGISTER macro arg |
| `outputKind` | describe() | get_operator | REGISTER macro variant |
| `module` | autoModule(__FILE__) | list_operators, get_operator | Auto from __FILE__ |
| `factory` | REGISTER macro | param introspection | REGISTER macro |
| `usage` | describe().withUsage() | get_operator | **Doxygen @code** |
| `examples` | describe().withExamples() | get_operator | **Doxygen @see** |
| `aliases` | describe().withAliases() | get_operator | **Remove** |
| `inputs` | describe().withInputs() | get_operator | **Doxygen @input** |
| **params** | introspected via factory() | get_operator | **Unchanged** |
| **headerPath** | (new) | get_operator | Auto from __FILE__ |

**Key insight:** GUI uses NONE of this metadata - only ParamDecl for sliders. All curated fields exist only for MCP/Claude.

## After Simplification

### Operator Registration (no describe())

```cpp
// BEFORE: describe() method required
class Displace : public TextureOperator {
public:
    static OperatorDescriptor describe() {
        return OperatorDescriptor("Displace", "Effects", "Texture displacement")
            .requireInput()
            .withInputs({
                {"source", "Texture to distort"},
                {"map", "Displacement map (R=X, G=Y)"}
            })
            .withUsage("auto& d = chain.add<Displace>(\"d\");\n...");
    }
};
REGISTER(Displace);

// AFTER: just a macro + Doxygen
/// @brief Texture displacement using a map
///
/// @input source Texture to distort
/// @input map Displacement map (R=X, G=Y)
///
/// @par Example
/// @code
/// auto& d = chain.add<Displace>("displace");
/// d.source("input");
/// d.map("noise");
/// d.strength = 0.1f;
/// @endcode
///
/// @see examples/distortion
class Displace : public TextureOperator { ... };

// In .cpp file:
REGISTER_OPERATOR(Displace, "Effects", "Texture displacement", true);
```

### Simplified REGISTER Macros

```cpp
// Most operators (texture output, no input required)
REGISTER_OPERATOR(Noise, "Generators", "Fractal noise generator", false);

// Operators requiring input
REGISTER_OPERATOR(Blur, "Effects", "Gaussian blur", true);

// Non-texture output (audio, value, etc.)
REGISTER_OPERATOR_EX(Oscillator, "Audio Synthesis", "Audio oscillator", false, OutputKind::Audio);

// Module is auto-detected from __FILE__, headerPath also derived from __FILE__
```

### What get_operator Returns

```json
{
  "name": "Displace",
  "category": "Effects",
  "description": "Texture displacement",
  "module": "vivid-core",
  "requiresInput": true,
  "outputType": "texture",
  "include": "#include <vivid/effects/displace.h>",
  "headerPath": "modules/vivid-core/include/vivid/effects/displace.h",
  "params": [
    {"name": "strength", "type": "float", "default": 0.1, "min": 0.0, "max": 1.0}
  ]
}
```

Claude reads `headerPath` for: inputs, usage examples, related operators, full API.

## MCP Tools

All 24 current tools stay. The simplification is in:
1. What `get_operator` returns (no more usage/examples/aliases/inputs)
2. Adding `headerPath` so Claude knows where to read more

## Implementation Steps

1. **Update REGISTER macros** to capture `__FILE__` and compute headerPath
2. **Simplify OperatorMeta** - remove: usage, examples, aliases, inputs fields
3. **Remove OperatorDescriptor** - no longer needed (macros provide all data)
4. **Remove describe() from all operators** (~40 operators)
5. **Update REGISTER calls** - switch from `REGISTER(Type)` to `REGISTER_OPERATOR(Type, Cat, Desc, ReqInput)`
6. **Add Doxygen comments** to operator headers with:
   - `@brief` description
   - `@input` for multi-input operators
   - `@par Example` + `@code` blocks
   - `@see` for related examples
7. **Update get_operator MCP handler** - return headerPath, remove curated fields
8. **Update operatorMetaToJson()** - add headerPath serialization
9. **Delete this file when complete**

## Files to Modify

### Core Infrastructure
- `modules/vivid-core/include/vivid/operator_registry.h` - simplify OperatorMeta, remove OperatorDescriptor, update macros
- `modules/vivid-core/src/operator_registry.cpp` - update operatorMetaToJson()
- `src/cli/mcp_server.cpp` - update get_operator handler

### Operator Headers (add Doxygen, remove describe())
- `modules/vivid-core/include/vivid/effects/*.h`
- `modules/vivid-audio/include/vivid/audio/*.h`
- `modules/vivid-video/include/vivid/video/*.h`
- `modules/vivid-render3d/include/vivid/render3d/*.h`
- `modules/vivid-network/include/vivid/network/*.h`

### Registration Files (update REGISTER calls)
- `modules/vivid-core/src/effects/operator_registrations.cpp`
- Various module .cpp files with REGISTER calls

## Verification

1. Build: `cmake --build build`
2. Run: `./build/bin/vivid projects/getting-started/02-hello-noise` - verify GUI works
3. Test MCP get_operator:
   ```bash
   echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_operator","arguments":{"name":"Noise"}}}' | ./build/bin/vivid mcp
   ```
   - Verify `headerPath` is returned
   - Verify `params` still have min/max/defaults
   - Verify no `usage`/`examples`/`aliases`/`inputs` fields
4. Verify headerPath is readable and contains Doxygen docs
5. Test list_operators still works
