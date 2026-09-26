// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file grid_wrapper_basic.h
\brief Compatibility include for the common grid backend concept definitions.

\details
Keeps existing include relationships stable while forwarding users to the backend-neutral grid concept
layer. It contains no physical grid implementation and therefore remains part of the framework-independent
C++ core.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Grid Models as the Next Step".
- "Grids as Ranges: The UI Loses Its Special Status".
- "The Grid as a Projection, Not as Truth".
- "From Text and Grid to a General Adapter Strategy".

\see ARCHITECTURE.md#grid-projection-and-ui-boundaries

\version 1.0
\date 26.09.2026
\author Volker Hillmann (adecc Systemhaus GmbH)

\copyright Copyright © 2021 - 2026 adecc Systemhaus GmbH

\licenseblock{LicenseRef-PolyForm-Noncommercial-1.0.0}
This file is licensed under the PolyForm Noncommercial License 1.0.0.
Use, modification, and distribution are permitted only as defined by that license.
The complete and controlling terms are available at
https://polyformproject.org/licenses/noncommercial/1.0.0/.
Any use not permitted by that license requires separate permission or a separate
license from adecc Systemhaus GmbH.
\endlicenseblock

*/

#pragma once

#include "grid_backend_concepts.h"
