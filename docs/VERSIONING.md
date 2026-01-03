# Versioning Conventions

Vivid follows [Semantic Versioning 2.0.0](https://semver.org/) with pre-release extensions for testing builds.

## Version Format

```
MAJOR.MINOR.PATCH[-PRERELEASE]

Examples:
  0.1.2          Stable release
  0.1.3-alpha.1  Alpha pre-release
  0.1.3-beta.1   Beta pre-release
  0.1.3-rc.1     Release candidate
  1.0.0          First stable major release
```

## Release Types

### Major Release (`X.0.0`)

**When to bump:** Breaking changes that require users to modify their code.

Examples:
- Removing or renaming public APIs
- Changing function signatures in incompatible ways
- Major architectural changes affecting user code
- Dropping support for a platform

```bash
git tag v1.0.0
git push origin v1.0.0
```

### Minor Release (`0.X.0`)

**When to bump:** New features that are backwards-compatible.

Examples:
- Adding new operators
- Adding new parameters to existing operators (with defaults)
- New addon functionality
- Performance improvements
- New example projects

```bash
git tag v0.2.0
git push origin v0.2.0
```

### Patch Release (`0.0.X`)

**When to bump:** Bug fixes and small improvements that are backwards-compatible.

Examples:
- Bug fixes
- Documentation updates
- Build system fixes
- Security patches
- Typo corrections

```bash
git tag v0.1.3
git push origin v0.1.3
```

## Pre-release Versions

Pre-releases are for testing before a stable release. They are **not** marked as "Latest" on GitHub and users must explicitly download them.

### Alpha (`-alpha.N`)

**Purpose:** Early development, may be unstable. For internal testing.

- Feature incomplete
- APIs may change
- Not recommended for production

```bash
git tag v0.2.0-alpha.1
git push origin v0.2.0-alpha.1
```

### Beta (`-beta.N`)

**Purpose:** Feature complete, testing phase. For adventurous users.

- All planned features implemented
- APIs stable but may have minor adjustments
- Known bugs may exist
- Good for user feedback

```bash
git tag v0.2.0-beta.1
git push origin v0.2.0-beta.1
```

### Release Candidate (`-rc.N`)

**Purpose:** Final testing before stable release. Should be production-ready.

- Feature frozen
- Only critical bug fixes allowed
- APIs locked
- If no issues found, becomes the stable release

```bash
git tag v0.2.0-rc.1
git push origin v0.2.0-rc.1
```

## Version Progression Example

```
v0.1.2           Current stable release
    │
    ├── Development begins on v0.2.0
    │
v0.2.0-alpha.1   Early testing (internal)
v0.2.0-alpha.2   More alpha builds
    │
    ├── Feature freeze
    │
v0.2.0-beta.1    Beta testing (public)
v0.2.0-beta.2    Bug fixes during beta
    │
    ├── Code freeze
    │
v0.2.0-rc.1      Release candidate
v0.2.0-rc.2      Critical fix (if needed)
    │
v0.2.0           Stable release (becomes "Latest")
```

## Hotfix Workflow

For critical bugs in stable releases:

```
v0.2.0           Stable release with critical bug
    │
    ├── Branch: git checkout -b hotfix/0.2.1 v0.2.0
    ├── Fix the bug
    ├── Merge to master
    │
v0.2.1           Hotfix release
```

If a hotfix needs testing:
```bash
git tag v0.2.1-rc.1    # Test the fix
git tag v0.2.1         # Release if OK
```

## GitHub Release Behavior

| Tag Format | GitHub Release Type | Shown as "Latest" |
|------------|---------------------|-------------------|
| `v1.2.3` | Release | Yes |
| `v1.2.3-alpha.1` | Pre-release | No |
| `v1.2.3-beta.1` | Pre-release | No |
| `v1.2.3-rc.1` | Pre-release | No |

The release workflow automatically detects pre-releases by checking for a hyphen in the tag.

## CI/CD Integration

### Automatic Triggers

| Event | Workflow | Result |
|-------|----------|--------|
| Push to `master` | CI | Build + test |
| Pull request | CI | Build + test |
| Tag `v*` | Release | Build + package + GitHub Release |

### Creating a Release

1. Ensure CI passes on `master`
2. Update `CHANGELOG.md` with release notes
3. Create and push the tag:
   ```bash
   git tag v0.2.0
   git push origin v0.2.0
   ```
4. Release workflow builds all platforms and creates GitHub Release

## Version in Code

The version is set via CMake at build time:

```cmake
# CMakeLists.txt
project(vivid VERSION 0.1.2)

# Override for releases
cmake -DVIVID_VERSION_OVERRIDE=0.2.0 ...
```

Access in C++:
```cpp
#include <vivid/version.h>
std::cout << vivid::version() << std::endl;  // "0.2.0"
```

## When to Release

- **Alpha**: When you want early feedback on new features
- **Beta**: When features are complete and you need broader testing
- **RC**: When you believe it's ready for release but want final validation
- **Stable**: When RC has been tested without critical issues
- **Hotfix**: Immediately for security issues; ASAP for critical bugs
