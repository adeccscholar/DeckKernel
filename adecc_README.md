# adecc C++ libraries

The header-only libraries currently in DeckKernel's `adecc/` directory
have moved to the standalone **adecc-core-lib** repository.

DeckKernel includes the repository as a Git submodule
under `adecc-core-lib/`. The standalone repository may contain its own `adecc/`
source directory; consequently the checkout path can be
`DeckKernel/adecc-core-lib/adecc/cpp_core/...`. A CMake `INTERFACE` target should
expose the outer repository directory so consumers keep includes such as
`#include <adecc/cpp_core/fixed_string.h>`.

## Licensing

The public source is governed by PolyForm Noncommercial License 1.0.0.
Eligible purchasers of **Rethinking C++ (C++ neu denken)** have an optional
additional MIT-like permissive license for the covered adecc-owned code,
including commercial and proprietary use.

See `adecc-core-lib/adecc/ADECC_BOOK_LICENSE.md` (or the same document in the standalone repository) for the full draft terms. The Book License
applies to the covered source independently of its Git checkout location;
it does **not** license the rest of DeckKernel or third-party components.

The material is provided primarily for teaching C++ techniques and
architecture. It comes **AS IS**, without warranty or any assurance of
fitness for production. A developer using it in production is responsible
for testing, security and suitability.

The license is a custom supplement, **not** the official MIT License.
A final legal review is still needed before publishing it as definitive
commercial licensing terms.
