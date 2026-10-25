# DeckKernel bootstrap

[TOC|Bootstrap]

This ZIP is laid out relative to the project repository root.  Copy/extract the
contained `bootstrap` and `Cache` directories directly into the project.

## 1. Required project layout before the first bootstrap

```text
<ProjectRoot>/
   bootstrap/
      Bootstrap.cmake
      BootstrapConfig.cmake
      PrepareDocumentationAssets.cmake

   Cache/
      thirdparty.lock
      thirdparty.lock.xml
      cmake/
         ThirdParty.cmake
         modules/
            FindBrotli.cmake
            Findnlohmann_json.cmake

   CMakeLists.txt
   ...
```

The two lock files come from StackBuilder and are not included in this ZIP.

The project-owned bootstrap configuration is kept in
`bootstrap/BootstrapConfig.cmake`. Its relevant current content is:

```cmake
if(NOT DEFINED ADECC_PACKAGE_REPOSITORY)
   set(ADECC_PACKAGE_REPOSITORY "https://github.com/adeccscholar/DeckKernel")
endif()

if(NOT DEFINED ADECC_REQUIRED_TOOLCHAIN)
   set(ADECC_REQUIRED_TOOLCHAIN "bcc64x")
endif()

if(NOT DEFINED ADECC_NINJA_VERSION)
   set(ADECC_NINJA_VERSION "1.13.2")
endif()
```

The package repository is a project-owned setting. End users do not have to enter a URL
or select a package source manually.

## 2. End-user prerequisites

- Windows
- C++Builder 13 / RAD Studio with BCC64X
- CMake 3.25 or newer, available from the C++Builder Developer Command Prompt
- Internet access for the first bootstrap run
- start a **C++Builder Developer Command Prompt**

**Ninja is required for the CMake/BCC64X build, but it does not have to be
installed manually.** The bootstrap requires Ninja 1.13.2. If exactly that
version is already available, it is reused. Otherwise the bootstrap downloads
the official Windows x64 Ninja archive from `ninja-build/ninja`, verifies the
pinned SHA-256 and archive size, and installs it locally below `Cache/tools`.

The bootstrap deliberately expects the Developer Command Prompt. It uses the
`BDS` environment variable and the active `cmake.exe` instead of asking a
beginner for installation paths.

## 3. One-command bootstrap

From the repository root:

```cmd
cmake -P bootstrap\Bootstrap.cmake
```

No C++ bootstrap program has to be compiled.

The bootstrap derives the repository root from the location of
`bootstrap/Bootstrap.cmake`.

## 4. What the bootstrap does

1. Reads `BDS` from the C++Builder Developer Command Prompt.
2. Records the **actually running** CMake from `${CMAKE_COMMAND}`.
3. Resolves `bcc64x.exe`.
4. Ensures the pinned Ninja 1.13.2 is available; if necessary it downloads and
   verifies the official Windows x64 release into `Cache/tools`.
5. Writes the local tool evidence to:

   ```text
   Cache/BootstrapTools.cmake
   ```

6. Reads `Cache/thirdparty.lock` (schema 2).
7. Verifies that the lock uses toolchain `bcc64x`.
8. Downloads missing GitHub Release assets into:

   ```text
   Cache/archives/
   ```

9. Verifies archive size and SHA-256.
10. Extracts each archive below:

   ```text
   ThirdParty/<package>/
   ```

   The archives are relative to the library root.  This preserves layouts such
   as:

   ```text
   ThirdParty/boost/
      1.92.0/
      bcc64x-native-clang/
   ```

11. Writes the relocatable package map:

    ```text
    Cache/ThirdPartyPackages.cmake
    ```

12. Prepares the browser-side documentation assets in the generated directory:

    ```text
    Docs/js/
    ```

    The bootstrap copies the DeckKernel-owned client from
    `src/docu_server/web/docu_client.js` and downloads the pinned browser
    libraries used by the documentation server:

    - highlight.js 11.12.0;
    - the GitHub highlight.js stylesheet;
    - Mermaid 11.17.2;
    - MathJax 3.2.2.

    `Docs/js` is shared by Debug and Release and is never a configuration-specific
    build output.

## 5. Generated tool-path file

A typical `Cache/BootstrapTools.cmake` contains:

```cmake
set(ADECC_BDS_ROOT "C:/Program Files (x86)/Embarcadero/Studio/37.0")
set(ADECC_BCC64X_EXECUTABLE "C:/Program Files (x86)/Embarcadero/Studio/37.0/bin64/bcc64x.exe")
set(ADECC_CMAKE_EXECUTABLE "C:/Users/.../CatalogRepository/CMake-cb/4.1.1/bin/cmake.exe")
set(ADECC_CMAKE_VERSION "4.1.1")
set(ADECC_NINJA_EXECUTABLE ".../Cache/tools/ninja/1.13.2/ninja.exe")
set(ADECC_NINJA_VERSION "1.13.2")
```

These are **results**, not values a beginner has to enter.

A later project CMake file can reuse them with:

```cmake
include("${CMAKE_SOURCE_DIR}/Cache/BootstrapTools.cmake")
```

## 6. Third-party integration in the project

Include this once, normally near the beginning of the top-level
`CMakeLists.txt` after the project has selected its configuration:

```cmake
include("${CMAKE_SOURCE_DIR}/Cache/cmake/ThirdParty.cmake")
```

`ThirdParty.cmake` then:

- reads the bootstrap-generated package roots,
- adds the exact BuildEngine include/library/runtime roots,
- uses BuildEngine-installed Config packages where they exist,
- adds the existing BuildEngine consumer modules for Brotli and nlohmann-json,
- handles the special Boost `bcc64x-native-clang` sibling,
- exposes OpenSSL and PostgreSQL/libpq paths to CMake's built-in Find modules.

The package versions are **not duplicated** in `ThirdParty.cmake`; they come
from `thirdparty.lock` through `ThirdPartyPackages.cmake`.

Examples:

```cmake
find_package(Boost REQUIRED)
find_package(Brotli REQUIRED)
find_package(BZip2 REQUIRED)
find_package(CURL REQUIRED)
find_package(LibArchive REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(libpqxx CONFIG REQUIRED)
find_package(libzip CONFIG REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(LibLZMA REQUIRED)
find_package(ZLIB REQUIRED)
find_package(zstd CONFIG REQUIRED)
```

For `cmark-gfm`, use the BuildEngine-installed Config package when required:

```cmake
find_package(cmark-gfm CONFIG REQUIRED)
```

## 7. Release/Debug selection

The installed BuildEngine packages contain configuration-specific binary
directories such as:

```text
lib/win64/Release
lib/win64/Debug
bin/win64/Release
bin/win64/Debug
```

`ThirdParty.cmake` uses:

- `Debug` when `CMAKE_BUILD_TYPE` is `Debug`
- otherwise `Release`

A project may explicitly set `ADECC_THIRDPARTY_CONFIGURATION` before including
`ThirdParty.cmake` if required.

Header-only packages, in particular nlohmann-json, are not forced into that
binary layout.  A CMake package supplied for nlohmann-json is searched directly
below `lib/cmake`.

## 8. Files to commit and files to ignore

Commit:

```text
bootstrap/Bootstrap.cmake
bootstrap/BootstrapConfig.cmake
bootstrap/PrepareDocumentationAssets.cmake
src/docu_server/web/docu_client.js
Cache/thirdparty.lock
Cache/thirdparty.lock.xml
Cache/cmake/**
```

Normally ignore:

```text
Cache/BootstrapTools.cmake
Cache/ThirdPartyPackages.cmake
Cache/archives/
Cache/bootstrap-state/
Cache/tools/
ThirdParty/
Docs/js/
```

## 9. Re-running

The bootstrap is idempotent:

- valid cached ZIPs are reused,
- SHA-256 and size are checked,
- an already extracted package is reused while its stored package hash matches,
- a changed package hash causes that package tree to be recreated.
- documentation browser assets are reused while their generated asset signature and
  expected files are present; changing one of the pinned browser versions recreates
  `Docs/js`.

To force one package to be extracted again, remove its corresponding file from:

```text
Cache/bootstrap-state/
```

or remove the package directory below `ThirdParty`.

## 10. Source of the CMake layout

The package integration reflects the existing BuildEngine installation
contract:

- `BuildEngine-Admin/admin/build-libraries.xml`
- `BuildEngine-Admin/admin/cmake/consumer/...`

It does not invent generic library names or reopen/repack the generated ZIP
files.

## Legacy release extension for Scantest and OCR

Until the new StackBuilder workflow replaces the old publishing path, use the
**existing** GitHub Release `2026.09.26-1` and its 17 verified assets unchanged.

`Cache/LegacyReleasePackages.xml` is a staging declaration for eight more
BCC64X packages pinned against `BuildEngine-Admin/admin/build-libraries.xml`:

| Package | Version | Direct package dependencies |
| --- | --- | --- |
| libpng | 1.6.59 | zlib 1.3.2 (already released) |
| libjpeg-turbo | 3.2.0 | — |
| libtiff | 4.7.2 | zlib, libjpeg-turbo |
| onetbb | 2023.1.0 | — |
| llvm-openmp | 20.1.7 | — |
| opencv | 5.0.0 | onetbb, llvm-openmp, zlib |
| leptonica | 1.87.0 | zlib, libpng, libjpeg-turbo, libtiff |
| tesseract | 5.5.3 | leptonica |

The existing release assets are **not regenerated or replaced**. The new
archive metadata must reflect actual package binaries; this repository does
not fabricate artifact sizes or SHA-256 hashes.

### Maintainer: stage packages from BuildEngine installations

Once all required BCC64X dynamic Release packages exist in the BuildEngine
installation, run from the DeckKernel root in a CMake-enabled command prompt:

```cmd
cmake -DLEGACY_INSTALL_ROOT=D:\local\embarcadero\test_v3\install\packages -DLEGACY_OUTPUT_ROOT=D:\local\DeckKernelLegacyRelease -P bootstrap\PackageLegacyRelease.cmake
```

Adjust both roots to the real machine; these are examples, not embedded paths.
The tool requires the pinned version directories, include trees and runtime DLLs.
It creates eight version-rooted ZIPs and a **candidate**
`legacy-release.lock` recording each archive's actual size and SHA-256.

Publish these exact ZIP files to the existing GitHub Release
[2026.09.26-1](https://github.com/adeccscholar/DeckKernel/releases/tag/2026.09.26-1).
After confirming that *all eight* assets exist with matching byte size and
SHA-256, copy the candidate lock to `Cache/legacy-release.lock` and commit it.
**Do not activate a partial candidate lock.** The repository intentionally
does not yet contain this lock while the assets are unpublished.

### Consumer: old bootstrap remains the only entry point

`bootstrap/Bootstrap.cmake` continues to verify the 17 original archives via
`thirdparty.lock` and its XML/selection consistency check. If and only if
`Cache/legacy-release.lock` exists, it additionally processes the eight
extra ZIPs through the same on-disk archive cache, exact-size/SHA verification,
version directories and generated `ADECC_TP_*_ROOT` package map.

No StackBuilder binary or PowerShell is required for this legacy route. Until
the extension assets are actually published, the old bootstrap keeps
working with the original 17 packages. The new OpenCV Scantest intentionally
fails to configure until its declared packages are installed; it must never
fall back to a different compiler or static libraries.

The ZIP packager and bootstrap integration are source changes and have not yet
been run in the actual Windows BCC64X build environment.
