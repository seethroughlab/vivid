# Vivid

A text-first creative coding framework with hot-reload and inline previews. Write C++ operators, see output at every step—designed for LLM-assisted development.

> **Note:** Currently rebuilding with Diligent Engine. See [ROADMAP.md](ROADMAP.md) for progress.

## Building

```bash
git clone --recursive https://github.com/seethroughlab/vivid
cd vivid
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

## Documentation

- [ROADMAP.md](ROADMAP.md) — Implementation plan
- [docs/PHILOSOPHY.md](docs/PHILOSOPHY.md) — Design principles and inspirations
- [docs/CHAIN-API.md](docs/CHAIN-API.md) — Chain API guide
- [docs/OPERATOR-API.md](docs/OPERATOR-API.md) — Creating custom operators
- [docs/OPERATORS.md](docs/OPERATORS.md) — Operator reference

## Requirements

- CMake 3.16+
- C++20 compiler (Clang 14+, GCC 11+, MSVC 2022+)
- Node.js 18+ (for VS Code extension)

## License

[MIT](LICENSE)
