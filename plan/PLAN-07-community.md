# PLAN-07: Community Operator Registry

A package management system for sharing and distributing Vivid operators, inspired by npm, pip, and Cargo.

## Overview

The community registry enables users to:
- **Share** operators they've created with others
- **Discover** operators built by the community
- **Install** packages with a single command
- **Manage** dependencies between packages

```
┌─────────────────────────────────────────────────────────────────┐
│                      VIVID ECOSYSTEM                             │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  User Project                     Registry Server               │
│  ┌─────────────────┐              ┌─────────────────┐          │
│  │ vivid.toml      │   publish    │ Package Index   │          │
│  │ chain.cpp       │ ──────────▶  │ Metadata Store  │          │
│  │ src/            │              │ File Storage    │          │
│  │ shaders/        │   install    │                 │          │
│  └─────────────────┘ ◀──────────  └─────────────────┘          │
│          │                               │                      │
│          ▼                               ▼                      │
│  ~/.vivid/packages/               registry.vivid.dev            │
│  ┌─────────────────┐              ┌─────────────────┐          │
│  │ noise-utils/    │              │ Web Interface   │          │
│  │ glitch-fx/      │              │ - Browse        │          │
│  │ vj-toolkit/     │              │ - Search        │          │
│  └─────────────────┘              │ - Documentation │          │
│                                   └─────────────────┘          │
└─────────────────────────────────────────────────────────────────┘
```

---

## Phase 1: Package Format Foundation

### Package Manifest (`vivid.toml`)

Every package contains a `vivid.toml` manifest file describing its contents:

```toml
[package]
name = "glitch-fx"
version = "1.0.0"
description = "A collection of glitch and datamosh effects for Vivid"
authors = ["Jane Doe <jane@example.com>"]
license = "MIT"
repository = "https://github.com/jane/vivid-glitch-fx"
homepage = "https://jane.github.io/vivid-glitch-fx"
documentation = "https://jane.github.io/vivid-glitch-fx/docs"
readme = "README.md"
keywords = ["glitch", "datamosh", "vj", "effects", "distortion"]
categories = ["effects", "video"]

[vivid]
version = ">=0.2.0"  # Minimum Vivid version required

[dependencies]
# Other Vivid packages this depends on
noise-utils = "^1.0"
# math-helpers = { version = ">=0.5", optional = true }

[build]
# Build configuration
cpp_standard = "20"
include_dirs = ["include"]

[[operators]]
name = "Glitch"
description = "Digital glitch effect with customizable intensity"
file = "src/glitch.cpp"
category = "effects"

[[operators]]
name = "Datamosh"
description = "Video datamosh/pixel sorting effect"
file = "src/datamosh.cpp"
category = "effects"

[[operators]]
name = "RGBSplit"
description = "Chromatic aberration with temporal offset"
file = "src/rgb_split.cpp"
category = "effects"

[[shaders]]
path = "shaders/glitch.wgsl"
description = "Core glitch shader"

[[shaders]]
path = "shaders/datamosh.wgsl"
description = "Datamosh pixel sorting shader"

[[shaders]]
path = "shaders/rgb_split.wgsl"
description = "RGB channel separation shader"

[[examples]]
name = "demo"
path = "examples/demo"
description = "Basic usage demonstration"

[[examples]]
name = "vj-set"
path = "examples/vj-set"
description = "Complete VJ performance setup"
```

### Package Directory Structure

```
my-package/
├── vivid.toml              # Package manifest (required)
├── README.md               # Package documentation (recommended)
├── LICENSE                 # License file (required for publishing)
├── CHANGELOG.md            # Version history (recommended)
├── src/
│   ├── glitch.cpp          # Operator implementations
│   ├── datamosh.cpp
│   └── rgb_split.cpp
├── include/                # Public headers (if any)
│   └── glitch_utils.h
├── shaders/
│   ├── glitch.wgsl         # WGSL shaders
│   ├── datamosh.wgsl
│   └── rgb_split.wgsl
├── assets/                 # Static assets (images, fonts, etc.)
│   └── lut.png
├── examples/
│   ├── demo/
│   │   ├── CMakeLists.txt
│   │   └── chain.cpp
│   └── vj-set/
│       ├── CMakeLists.txt
│       └── chain.cpp
└── tests/                  # Package tests (optional)
    └── test_glitch.cpp
```

### Manifest Schema

| Field | Required | Description |
|-------|----------|-------------|
| `package.name` | Yes | Package identifier (lowercase, hyphens allowed) |
| `package.version` | Yes | Semantic version (MAJOR.MINOR.PATCH) |
| `package.description` | Yes | Short description (max 140 chars) |
| `package.authors` | Yes | List of author names/emails |
| `package.license` | Yes | SPDX license identifier |
| `package.repository` | No | Source code repository URL |
| `package.keywords` | No | Search keywords (max 10) |
| `package.categories` | No | Package categories |
| `vivid.version` | Yes | Compatible Vivid version constraint |
| `dependencies` | No | Other packages required |
| `operators` | Yes | List of exported operators |
| `shaders` | No | List of included shaders |
| `examples` | No | List of example projects |

### Name Validation Rules

- Lowercase letters, numbers, and hyphens only
- Must start with a letter
- 3-64 characters
- Cannot be a reserved word (`vivid`, `core`, `std`, etc.)
- Optionally scoped: `@username/package-name`

---

## Phase 2: Local Package Management

### Package Cache Directory

```
~/.vivid/
├── config.toml             # Global configuration
├── credentials.toml        # Registry authentication (encrypted)
├── cache/
│   └── downloads/          # Downloaded package archives
│       └── glitch-fx-1.0.0.tar.gz
├── packages/               # Installed packages
│   ├── glitch-fx/
│   │   └── 1.0.0/
│   │       ├── vivid.toml
│   │       ├── src/
│   │       └── shaders/
│   └── noise-utils/
│       ├── 1.0.0/
│       └── 1.1.0/
└── index/                  # Local package index cache
    └── registry.json
```

### Global Configuration (`~/.vivid/config.toml`)

```toml
[registry]
default = "https://registry.vivid.dev"

[install]
# Where to install packages for the current user
packages_dir = "~/.vivid/packages"

[build]
# Default build settings
jobs = 4  # Parallel compilation jobs

[network]
timeout = 30  # HTTP timeout in seconds
retries = 3
```

### CLI Commands

#### `vivid init`
Create a new package:

```bash
$ vivid init my-effects
Creating new Vivid package: my-effects

Package name: my-effects
Version: 0.1.0
Description: My custom effects
Author: Your Name <you@example.com>
License: MIT

Created my-effects/
  vivid.toml
  README.md
  src/
  shaders/
  examples/demo/

Next steps:
  cd my-effects
  # Add your operators to src/
  # Add your shaders to shaders/
  vivid build  # Build the package
```

#### `vivid install`
Install packages:

```bash
# Install from registry
$ vivid install glitch-fx
Installing glitch-fx@1.0.0...
  Downloading glitch-fx-1.0.0.tar.gz
  Installing dependencies: noise-utils@1.0.0
  Done! Installed 2 packages.

# Install specific version
$ vivid install glitch-fx@0.9.0

# Install from git
$ vivid install git+https://github.com/jane/vivid-glitch-fx.git

# Install from local path
$ vivid install ./my-local-package

# Install with optional dependencies
$ vivid install glitch-fx --with-optional
```

#### `vivid uninstall`
Remove packages:

```bash
$ vivid uninstall glitch-fx
Uninstalling glitch-fx@1.0.0...
  Removed glitch-fx
  Note: noise-utils@1.0.0 is still required by other packages
```

#### `vivid list`
List installed packages:

```bash
$ vivid list
Installed packages:
  glitch-fx      1.0.0   A collection of glitch effects
  noise-utils    1.0.0   Noise generation utilities
  vj-toolkit     2.1.0   Complete VJ performance toolkit

$ vivid list --outdated
Outdated packages:
  Package        Current  Latest
  glitch-fx      1.0.0    1.2.0
  vj-toolkit     2.1.0    3.0.0
```

#### `vivid update`
Update packages:

```bash
$ vivid update
Updating packages...
  glitch-fx: 1.0.0 -> 1.2.0
  vj-toolkit: 2.1.0 -> 3.0.0

$ vivid update glitch-fx  # Update specific package
```

#### `vivid search`
Search the registry:

```bash
$ vivid search glitch
Results for "glitch":
  glitch-fx       1.2.0   A collection of glitch effects
  glitch-art      0.5.0   Generative glitch art operators
  vhs-glitch      1.0.0   VHS tape degradation effects

$ vivid search --category effects
$ vivid search --author jane
```

#### `vivid info`
Show package details:

```bash
$ vivid info glitch-fx
glitch-fx 1.2.0
A collection of glitch and datamosh effects for Vivid

Homepage:    https://jane.github.io/vivid-glitch-fx
Repository:  https://github.com/jane/vivid-glitch-fx
License:     MIT
Authors:     Jane Doe <jane@example.com>

Operators:
  - Glitch      Digital glitch effect
  - Datamosh    Video datamosh effect
  - RGBSplit    Chromatic aberration

Dependencies:
  - noise-utils ^1.0

Keywords: glitch, datamosh, vj, effects
```

#### `vivid build`
Build a package:

```bash
$ vivid build
Building glitch-fx@1.0.0...
  Compiling src/glitch.cpp
  Compiling src/datamosh.cpp
  Compiling src/rgb_split.cpp
  Linking libglitch-fx.dylib
  Done! Built in 2.3s

$ vivid build --release  # Optimized build
```

---

## Phase 3: Registry Infrastructure

### Registry API Design

RESTful API for the package registry:

#### Authentication

```
POST /api/v1/auth/login
POST /api/v1/auth/register
POST /api/v1/auth/logout
GET  /api/v1/auth/me
POST /api/v1/auth/tokens          # Create API token
DELETE /api/v1/auth/tokens/:id    # Revoke token
```

#### Packages

```
GET    /api/v1/packages                    # List packages
GET    /api/v1/packages/:name              # Get package info
GET    /api/v1/packages/:name/:version     # Get specific version
GET    /api/v1/packages/:name/versions     # List all versions
POST   /api/v1/packages                    # Publish new package
PUT    /api/v1/packages/:name/:version     # Update metadata
DELETE /api/v1/packages/:name/:version     # Yank version (soft delete)
```

#### Downloads

```
GET /api/v1/packages/:name/:version/download   # Download package archive
GET /api/v1/packages/:name/:version/checksum   # Get SHA256 checksum
```

#### Search

```
GET /api/v1/search?q=glitch                    # Full-text search
GET /api/v1/search?category=effects            # By category
GET /api/v1/search?author=jane                 # By author
GET /api/v1/search?keyword=vj                  # By keyword
```

#### Users

```
GET  /api/v1/users/:username                   # User profile
GET  /api/v1/users/:username/packages          # User's packages
```

### Package Index Format

The registry maintains a package index that can be synced locally:

```json
{
  "packages": {
    "glitch-fx": {
      "name": "glitch-fx",
      "description": "A collection of glitch effects",
      "latest": "1.2.0",
      "versions": {
        "1.0.0": {
          "vivid": ">=0.2.0",
          "checksum": "sha256:abc123...",
          "dependencies": {"noise-utils": "^1.0"},
          "published": "2024-01-15T10:30:00Z"
        },
        "1.2.0": {
          "vivid": ">=0.2.0",
          "checksum": "sha256:def456...",
          "dependencies": {"noise-utils": "^1.0"},
          "published": "2024-03-20T14:00:00Z"
        }
      },
      "owners": ["jane"],
      "repository": "https://github.com/jane/vivid-glitch-fx"
    }
  },
  "updated": "2024-03-20T14:00:00Z"
}
```

### Registry Server Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      Registry Server                             │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────┐   ┌─────────────┐   ┌─────────────┐           │
│  │   API       │   │  Auth       │   │  Search     │           │
│  │   Server    │   │  Service    │   │  Service    │           │
│  │  (REST)     │   │  (JWT)      │   │ (Meilisearch)│          │
│  └──────┬──────┘   └──────┬──────┘   └──────┬──────┘           │
│         │                 │                 │                   │
│         └────────────┬────┴────────────────┘                   │
│                      │                                          │
│              ┌───────▼───────┐                                  │
│              │   Database    │                                  │
│              │  (PostgreSQL) │                                  │
│              └───────────────┘                                  │
│                      │                                          │
│              ┌───────▼───────┐                                  │
│              │ File Storage  │                                  │
│              │  (S3/MinIO)   │                                  │
│              └───────────────┘                                  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Phase 4: Publishing & Distribution

### Publishing Workflow

```bash
# 1. Create account (one-time)
$ vivid register
Email: jane@example.com
Username: jane
Password: ********
Account created! Check your email to verify.

# 2. Login
$ vivid login
Email: jane@example.com
Password: ********
Logged in as jane

# 3. Validate package
$ vivid check
Checking glitch-fx@1.0.0...
  ✓ vivid.toml is valid
  ✓ All operators compile
  ✓ All shaders compile
  ✓ README.md exists
  ✓ LICENSE exists
  ✓ No security issues found
Package is ready to publish!

# 4. Publish
$ vivid publish
Publishing glitch-fx@1.0.0...
  Packaging files...
  Uploading to registry...
  Published! https://registry.vivid.dev/packages/glitch-fx
```

### Pre-Publish Validation

Before publishing, the CLI validates:

1. **Manifest Completeness**
   - All required fields present
   - Valid semantic version
   - Valid SPDX license

2. **Build Success**
   - All operators compile
   - All shaders compile
   - No undefined symbols

3. **Documentation**
   - README.md exists and is non-empty
   - LICENSE file exists

4. **Security Scan**
   - No embedded credentials
   - No suspicious code patterns
   - Dependencies are not yanked

5. **Size Limits**
   - Package under 50MB
   - No large binary files

### Version Yanking

Yank a version to prevent new installations (doesn't delete, existing users can still use):

```bash
$ vivid yank glitch-fx@1.0.0 --reason "Security vulnerability"
Yanked glitch-fx@1.0.0
Note: Existing installations are not affected.
```

### Package Ownership

```bash
# Add co-owner
$ vivid owner add glitch-fx bob
Added bob as owner of glitch-fx

# Remove owner
$ vivid owner remove glitch-fx bob
Removed bob from glitch-fx owners

# List owners
$ vivid owner list glitch-fx
Owners of glitch-fx:
  jane (primary)
  bob
```

---

## Phase 5: Web Interface

### Features

1. **Package Browser**
   - Featured packages
   - Recently updated
   - Most downloaded
   - By category

2. **Package Detail Page**
   - README rendering (markdown)
   - Operator documentation
   - Version history
   - Download statistics
   - Dependency graph
   - GitHub integration (stars, issues)

3. **Search**
   - Full-text search
   - Filters (category, keywords, author)
   - Sort (downloads, recent, relevance)

4. **User Profiles**
   - Published packages
   - Activity feed
   - Contact info

5. **Documentation**
   - Getting started guide
   - Publishing guide
   - API reference
   - Best practices

### URL Structure

```
registry.vivid.dev/
├── /                           # Homepage
├── /packages                   # Browse all packages
├── /packages/glitch-fx         # Package detail
├── /packages/glitch-fx/1.0.0   # Specific version
├── /users/jane                 # User profile
├── /search?q=...               # Search results
├── /categories                 # Category listing
├── /docs                       # Documentation
└── /api                        # API documentation
```

---

## Phase 6: Advanced Features

### Semantic Version Resolution

Support version constraints like npm/Cargo:

| Constraint | Meaning |
|------------|---------|
| `1.0.0` | Exactly 1.0.0 |
| `^1.0.0` | >=1.0.0 and <2.0.0 (compatible) |
| `~1.0.0` | >=1.0.0 and <1.1.0 (patch updates) |
| `>=1.0.0` | 1.0.0 or higher |
| `>=1.0.0, <2.0.0` | Range |
| `*` | Any version |

### Dependency Resolution Algorithm

1. Build dependency graph
2. Check for conflicts
3. Find compatible version set (SAT solver)
4. Report unresolvable conflicts clearly

### Package Namespacing

Support scoped packages for organizations/users:

```toml
[package]
name = "@jane/glitch-fx"  # Scoped to user
# or
name = "@mycompany/internal-utils"  # Scoped to org
```

### Package Signing

Optional GPG signing for verified packages:

```bash
$ vivid publish --sign
Publishing glitch-fx@1.0.0...
  Signing package with GPG key...
  Published with signature!

$ vivid install glitch-fx --verify
Installing glitch-fx@1.0.0...
  ✓ Signature verified (jane@example.com)
```

### Private Registries

Support private/enterprise registries:

```toml
# ~/.vivid/config.toml
[[registries]]
name = "mycompany"
url = "https://vivid.mycompany.com"
token = "env:MYCOMPANY_VIVID_TOKEN"

# In project vivid.toml
[dependencies]
public-package = "^1.0"  # From default registry
"mycompany:internal-utils" = "^2.0"  # From private registry
```

---

## Implementation Phases

### Now: Foundation (Pre-registry)
- [ ] Define and document `vivid.toml` schema
- [ ] Document package directory structure
- [ ] Create package template generator

### Near-term: Local Tooling
- [ ] `vivid init` command
- [ ] `vivid build` command
- [ ] `vivid install` from local/git
- [ ] Package cache directory

### Medium-term: Registry MVP
- [ ] Registry server (basic API)
- [ ] `vivid publish` command
- [ ] `vivid search` command
- [ ] Basic web interface

### Long-term: Full Ecosystem
- [ ] Version resolution
- [ ] Dependency graphs
- [ ] Package signing
- [ ] Private registries
- [ ] Organization accounts

---

## Security Considerations

1. **Package Validation**
   - Scan for malicious code patterns
   - Check for credential leaks
   - Validate all file paths (no path traversal)

2. **Authentication**
   - Strong password requirements
   - 2FA support
   - API tokens with scopes

3. **Supply Chain**
   - Checksum verification
   - Optional package signing
   - Audit logs

4. **Rate Limiting**
   - Download limits
   - Publish limits
   - API rate limiting

---

## References

- [npm Registry Architecture](https://docs.npmjs.com/)
- [Cargo (Rust) Package Manager](https://doc.rust-lang.org/cargo/)
- [PyPI (Python)](https://pypi.org/)
- [TOML Specification](https://toml.io/)
- [Semantic Versioning](https://semver.org/)
