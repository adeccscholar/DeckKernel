// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file stream_redirect.h
\brief RAII redirection of standard C++ streams to alternate stream buffers.

\details
Temporarily replaces a stream's rdbuf and reliably restores the previous buffer when the guard leaves scope.
The helper demonstrates how changed runtime state can be made explicit and exception-safe through RAII.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Typed Runtime Structure and RAII".
- "stream_redirect: RAII over Changed State".
- "Files as Ranges: Resources, Tuples, and RAII".
- "The Cost Model of Abstraction".

\see ARCHITECTURE.md#raii-and-deterministic-state

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

#include <ostream>
#include <streambuf>
#include <type_traits>
#include <concepts>
#include <stdexcept>

namespace adecc {

/*!
   \brief RAII redirector from standard streams to arbitrary stream buffers
   \details
      \tparam CharT   Character type of the target stream (char, wchar_t, ...)
      \tparam Traits  Traits type of the target stream
      \note
         - The constructor replaces the target stream's rdbuf with the supplied stream buffer.
         - The destructor flushes and restores the previous rdbuf.
         - The guard is movable but not copyable.
         - A convenience constructor accepts derived stream-buffer types when their
           \c Ty::streambuf matches \c std::basic_ostream<CharT, Traits>.
*/
/*!
   \brief RAII redirector from standard streams to arbitrary stream buffers
   \details
      \tparam CharT Character type (char, wchar_t, ...).
      \tparam Traits Traits type
      \note
         - Pointer constructor: redirects rdbuf to pNewBuf.
         - Reference constructor: convenience overload for derived buffers.
         - Constrained constructor: accepts compatible derived buffer types.
         - Destructor: flushes and restores the original rdbuf.
*/
template <typename CharT, typename Traits = std::char_traits<CharT>>
class StreamRedirect {
public:
   using stream_type    = std::basic_ostream<CharT, Traits>;
   using streambuf_type = std::basic_streambuf<CharT, Traits>;

   /*! \brief Primary constructor: redirects rdbuf to pNewBuf. */
   StreamRedirect(stream_type & theStream, streambuf_type *const pNewBuf)
      : pStream{&theStream}, pOldBuf{theStream.rdbuf()} {
      if(pNewBuf == nullptr) {
         throw std::invalid_argument("StreamRedirect: pNewBuf must not be null");
         }
      pStream->rdbuf(pNewBuf);
      }

   /*! \brief Convenience constructor accepting a compatible streambuf reference. */
   StreamRedirect(stream_type & theStream, streambuf_type & theBuf)
      : StreamRedirect(theStream, std::addressof(theBuf)) { }

   /*! \brief SFINAE constructor accepting any buffer derived from streambuf_type. */
   template <typename BufT,
             typename = std::enable_if_t<
                std::is_base_of_v<streambuf_type, std::remove_reference_t<BufT>>>>
   StreamRedirect(stream_type & theStream, BufT & theBuf)
      : StreamRedirect(theStream, static_cast<streambuf_type &>(theBuf)) { }

   ~StreamRedirect(void) {
      if(pStream != nullptr) {
         pStream->flush();
         pStream->rdbuf(pOldBuf);
      }
      pStream = nullptr;
      pOldBuf = nullptr;
      }

   StreamRedirect(StreamRedirect const &) = delete;
   StreamRedirect & operator=(StreamRedirect const &) = delete;

   StreamRedirect(StreamRedirect && other) noexcept
      : pStream{other.pStream}, pOldBuf{other.pOldBuf} {
      other.pStream = nullptr;
      other.pOldBuf = nullptr;
      }

   StreamRedirect & operator=(StreamRedirect && other) noexcept {
      if(this != &other) {
         if(pStream != nullptr) {
            pStream->flush();
            pStream->rdbuf(pOldBuf);
            }
         pStream = other.pStream;
         pOldBuf = other.pOldBuf;
         other.pStream = nullptr;
         other.pOldBuf = nullptr;
         }
      return *this;
      }

   /*! \brief Restores the original stream buffer early. */
   void Reset() {
      if(pStream != nullptr) {
         pStream->flush();
         pStream->rdbuf(pOldBuf);
         pStream = nullptr;
         pOldBuf = nullptr;
         }
      }

private:
   stream_type    *pStream{};
   streambuf_type *pOldBuf{};
};


} // namespace adecc

