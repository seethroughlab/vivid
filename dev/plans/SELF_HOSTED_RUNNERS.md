# Self-Hosted GitHub Actions Runners Plan

## Problem

$114.95/month on GitHub Actions, primarily driven by:
- **macOS runners**: $0.08/min (10x Linux cost) - ~80% of spend
- **Full CI on every push to master**: ~$2.30 per run
- **No caching**: Full rebuilds every time

## Solution

Set up self-hosted runners on available hardware to reduce costs to ~$0/month.

### Available Hardware

| Platform | Machine | Availability | Notes |
|----------|---------|--------------|-------|
| macOS | Personal MacBook | During dev hours | On when CI typically runs |
| Windows | Windows PC | 24/7 | Always on |
| Linux x64 | OpenMediaVault | 24/7 | Always on |
| Linux ARM64 | Raspberry Pi | 24/7 (optional) | Native builds (no cross-compile) |

### Cost Comparison

| Scenario | Monthly Cost |
|----------|--------------|
| Current (GitHub-hosted) | ~$115 |
| Self-hosted Mac + Windows + Linux | ~$0-5 |

## Implementation Phases

### Phase 1: Set Up Self-Hosted Runners

Each machine needs the GitHub Actions runner agent installed.

#### 1.1 Mac Runner Setup

```bash
# On your Mac
mkdir -p ~/actions-runner && cd ~/actions-runner
curl -o actions-runner.tar.gz -L https://github.com/actions/runner/releases/download/v2.XXX/actions-runner-osx-x64-2.XXX.tar.gz
tar xzf actions-runner.tar.gz

# Configure (get token from GitHub repo settings)
./config.sh --url https://github.com/seethroughlab/vivid --token YOUR_TOKEN

# Run as service (starts on login)
./svc.sh install
./svc.sh start
```

Labels: `self-hosted`, `macOS`, `ARM64`

#### 1.2 Windows Runner Setup

```powershell
# On Windows (PowerShell as Admin)
mkdir C:\actions-runner; cd C:\actions-runner
Invoke-WebRequest -Uri https://github.com/actions/runner/releases/download/v2.XXX/actions-runner-win-x64-2.XXX.zip -OutFile runner.zip
Expand-Archive runner.zip -DestinationPath .

# Configure
.\config.cmd --url https://github.com/seethroughlab/vivid --token YOUR_TOKEN

# Install as Windows service
.\svc.cmd install
.\svc.cmd start
```

Labels: `self-hosted`, `Windows`, `X64`

#### 1.3 Linux Runner Setup (OpenMediaVault)

```bash
# SSH to OpenMediaVault
mkdir -p ~/actions-runner && cd ~/actions-runner
curl -o actions-runner.tar.gz -L https://github.com/actions/runner/releases/download/v2.XXX/actions-runner-linux-x64-2.XXX.tar.gz
tar xzf actions-runner.tar.gz

# Install dependencies
sudo ./bin/installdependencies.sh

# Configure
./config.sh --url https://github.com/seethroughlab/vivid --token YOUR_TOKEN

# Install as systemd service
sudo ./svc.sh install
sudo ./svc.sh start
```

Labels: `self-hosted`, `Linux`, `X64`

#### 1.4 Raspberry Pi Runner (Optional)

```bash
# SSH to Pi
mkdir -p ~/actions-runner && cd ~/actions-runner
curl -o actions-runner.tar.gz -L https://github.com/actions/runner/releases/download/v2.XXX/actions-runner-linux-arm64-2.XXX.tar.gz
tar xzf actions-runner.tar.gz

./bin/installdependencies.sh
./config.sh --url https://github.com/seethroughlab/vivid --token YOUR_TOKEN
sudo ./svc.sh install
sudo ./svc.sh start
```

Labels: `self-hosted`, `Linux`, `ARM64`

### Phase 2: Update CI Workflow

Modify `.github/workflows/ci.yml` to use self-hosted runners:

```yaml
name: CI

on:
  push:
    branches: [master, main]
  pull_request:
    branches: [master, main]

jobs:
  build:
    name: Build (${{ matrix.target }})
    runs-on: ${{ matrix.runner }}
    strategy:
      fail-fast: false
      matrix:
        include:
          - target: linux-x64
            runner: [self-hosted, Linux, X64]
          - target: macos-arm64
            runner: [self-hosted, macOS, ARM64]
          - target: windows-x64
            runner: [self-hosted, Windows, X64]

    steps:
      - name: Checkout
        uses: actions/checkout@v4

      # No need to download wgpu-native - it's cached on the machine
      # No need to install deps - they're pre-installed

      - name: Configure CMake
        run: cmake -B build -DBUILD_TESTS=ON

      - name: Build
        run: cmake --build build --config Release --parallel

      - name: Run tests
        run: ctest --test-dir build --output-on-failure -C Release

  # Optional: Keep Raspberry Pi on GitHub-hosted for cross-compile
  # Or use self-hosted Pi for native builds
  build-raspberry-pi:
    name: Build (Raspberry Pi ARM64)
    runs-on: [self-hosted, Linux, ARM64]  # Native Pi build
    # OR keep: runs-on: ubuntu-latest      # Cross-compile (costs ~$0.15/run)
    steps:
      - uses: actions/checkout@v4
      - run: cmake -B build -DBUILD_TESTS=OFF
      - run: cmake --build build --config Release --parallel
```

### Phase 3: Update Release Workflow

Modify `.github/workflows/release.yml`:

```yaml
jobs:
  build-release:
    name: Build (${{ matrix.target }})
    runs-on: ${{ matrix.runner }}
    strategy:
      matrix:
        include:
          - target: darwin-arm64
            runner: [self-hosted, macOS, ARM64]
          - target: linux-x64
            runner: [self-hosted, Linux, X64]
          - target: windows-x64
            runner: [self-hosted, Windows, X64]
          # Note: darwin-x64 cross-compile removed (or do on Mac with -DCMAKE_OSX_ARCHITECTURES=x86_64)
```

### Phase 4: Pre-Install Dependencies

On each self-hosted runner, pre-install build dependencies so CI is faster:

#### Mac
```bash
xcode-select --install
brew install cmake ninja
```

#### Windows
- Install Visual Studio 2022 Build Tools
- Install CMake
- Install Git

#### Linux (OpenMediaVault)
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build \
  libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libxext-dev libwayland-dev libxkbcommon-dev libgl1-mesa-dev
```

#### Raspberry Pi
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake \
  libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libxext-dev libwayland-dev libxkbcommon-dev libgl1-mesa-dev
```

### Phase 5: Add Caching (Optional Optimization)

Even with self-hosted runners, caching speeds up builds:

```yaml
- name: Cache CMake dependencies
  uses: actions/cache@v4
  with:
    path: |
      build/_deps
    key: deps-${{ runner.os }}-${{ hashFiles('CMakeLists.txt') }}
```

Or just keep the `build/_deps` directory on each machine between runs.

## Fallback Strategy

If a self-hosted runner is offline, jobs will queue indefinitely. Options:

1. **Accept it**: Re-run when machine is back online
2. **Hybrid approach**: Use GitHub-hosted as fallback for critical branches
3. **Wake-on-LAN**: Script to wake machines when jobs queue

### Hybrid Example (Linux fallback)

```yaml
jobs:
  build-linux:
    runs-on: ${{ github.event_name == 'push' && 'ubuntu-latest' || fromJSON('["self-hosted", "Linux", "X64"]') }}
```

This uses self-hosted for PRs (faster, free) but GitHub-hosted for pushes to master (guaranteed availability).

## Security Considerations

Self-hosted runners execute code from the repository. For a private repo you control, this is fine. Considerations:

- Don't use self-hosted runners for public repos (anyone can submit PRs)
- Runners have access to the machine's filesystem
- Keep runner software updated

## Maintenance

- **Updates**: Runners auto-update, but check occasionally
- **Disk space**: Clean old build artifacts periodically
- **Logs**: Runner logs are in `~/actions-runner/_diag/`

## Files to Modify

1. `.github/workflows/ci.yml` - Switch to self-hosted runners
2. `.github/workflows/release.yml` - Switch to self-hosted runners

## Estimated Savings

| Before | After | Savings |
|--------|-------|---------|
| ~$115/month | ~$0-5/month | ~$110/month (95%+) |

The only remaining costs would be:
- GitHub-hosted Raspberry Pi cross-compile (if not using Pi runner): ~$5/month
- Occasional GitHub-hosted fallback runs: ~$0-5/month
