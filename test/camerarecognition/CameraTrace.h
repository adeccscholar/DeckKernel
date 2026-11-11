// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once
/**
\file CameraTrace.h
\brief Opt-in diagnostic output for the camera recognition experiment.
\details Routine camera, matcher, OCR and database diagnostics are silent
   unless the command line explicitly enables --verbose. User-visible
   recognition state remains available in the dashboard.
*/
#include <atomic>
#include <format>
#include <print>
#include <utility>

namespace deckkernel::camera {
   inline std::atomic_bool g_boCameraTrace{false};
   inline std::atomic_bool g_boDeepSearch{false};

   /**
   \brief Enable the legacy global full-card fallback independently of logs.
   \details Normal live scans skip this expensive, weakly discriminating pass.
   \param boEnabled Explicit --deep-search option.
   \returns Nothing.
   \throw None.
   */
   inline void SetDeepSearchEnabled(bool boEnabled) noexcept {
      g_boDeepSearch.store(boEnabled,std::memory_order_relaxed);
   }

   /**
   \brief Test whether exhaustive full-card matching is requested.
   \returns True for --deep-search.
   \throw None.
   */
   [[nodiscard]] inline bool DeepSearchEnabled() noexcept {
      return g_boDeepSearch.load(std::memory_order_relaxed);
   }

   /**
   \brief Control expensive diagnostic console output.
   \details Configure before starting the image-matching worker.
   \param boEnabled Explicit --verbose flag.
   \returns Nothing.
   \throw None.
   */
   inline void SetTraceEnabled(bool boEnabled) noexcept {
      g_boCameraTrace.store(boEnabled,std::memory_order_relaxed);
   }

   /**
   \brief Test whether diagnostic output and index profiling are requested.
   \details A relaxed atomic read suffices because startup sets the flag.
   \returns True only for --verbose.
   \throw None.
   */
   [[nodiscard]] inline bool TraceEnabled() noexcept {
      return g_boCameraTrace.load(std::memory_order_relaxed);
   }

   /**
   \brief Print a diagnostic message only when explicitly requested.
   \details Formatting and writes are skipped in ordinary live scanning.
   \tparam tyArgs Argument types for compile-time format validation.
   \param aFormat C++23 format string.
   \param aArgs Values supplied to the format string.
   \returns Nothing.
   \throw std::format_error If format processing fails.
   */
   template <typename... tyArgs>
   inline void Trace(std::format_string<tyArgs...> aFormat,
      tyArgs&&... aArgs) {
      if(!TraceEnabled())return;
      std::println(aFormat,std::forward<tyArgs>(aArgs)...);
   }
}
