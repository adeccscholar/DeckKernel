// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file generator.h
\brief Coroutine-based pull generator used as a lazy typed source.

\details
Implements a move-only generator with iterator integration, value buffering, exception propagation, and
explicit coroutine lifetime management. It supports lazy data movement without forcing materialization and
is used by file and database-related flows.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Source and Sink: Data Flows in Type Space".
- "Ranges as a Universal Data Model".
- "Ranges, Lazy Processing, and Materialization".
- "Data Movement Between Source and Sink".

\see ARCHITECTURE.md#source-transformation-and-sink

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

#include <coroutine>
#include <concepts>
#include <exception>
#include <ranges>
#include <type_traits>
#include <utility>
#include <stop_token>
#include <cstdint>

namespace adecc {

   template <typename ty>
   class SimpleGenerator {
   public:
      struct promise_type {
         using handle_type = std::coroutine_handle<promise_type>;

         std::optional<ty>        current_;
         std::exception_ptr      error_;

         // Required: creates the generator object for the caller
         SimpleGenerator get_return_object() noexcept {
            return SimpleGenerator{ handle_type::from_promise(*this) };
         }
         // Start/end: the caller explicitly resumes the coroutine
         std::suspend_always initial_suspend() const noexcept { return {}; }
         std::suspend_always final_suspend()   const noexcept { return {}; }

         // No explicit return value for a pull generator
         void return_void() noexcept {}

         // Value transfer: buffer ty by value
         std::suspend_always yield_value(ty value) noexcept(std::is_nothrow_move_constructible_v<ty>) {
            current_.emplace(std::move(value));
            return {};
         }

         // Store exceptions in the promise and rethrow them later
         void unhandled_exception() noexcept { error_ = std::current_exception(); }

         // C++20: no await_transform is required
      };

      using handle_type = std::coroutine_handle<promise_type>;

      // ——— CTORs/DTORs/Assign ————————————————————————————————————————————————
      explicit SimpleGenerator(handle_type h = nullptr) noexcept : h_(h) {}

      SimpleGenerator(SimpleGenerator&& other) noexcept : h_(std::exchange(other.h_, {})) {}
      SimpleGenerator& operator=(SimpleGenerator&& other) noexcept {
         if (this != &other) {
            destroy_();
            h_ = std::exchange(other.h_, {});
         }
         return *this;
      }

      SimpleGenerator(SimpleGenerator const&) = delete;
      SimpleGenerator& operator=(SimpleGenerator const&) = delete;

      ~SimpleGenerator() { destroy_(); }

      // ——— Iterator (input-iterator) ————————————————————————————————————————
      class iterator {
      public:
         using iterator_concept = std::input_iterator_tag;
         using difference_type = std::ptrdiff_t;
         using value_type = ty;

         iterator() noexcept = default;
         explicit iterator(handle_type h) : h_(h) {
            advance_();  // beim Begin direkt zur ersten yield-Position
         }

         // Sentinel-Style
         friend bool operator==(iterator const& it, std::default_sentinel_t) noexcept { return !it.h_; }

         // Dereference returns a reference to the buffered value
         value_type const& operator*() const {
            rethrow_if_failed_();
            return *(h_.promise().current_);
         }

         value_type const* operator->() const {
            rethrow_if_failed_();
            return std::addressof(*(h_.promise().current_));
         }

         // ++ advances to the next yield position
         iterator& operator++() {
            rethrow_if_failed_();
            h_.promise().current_.reset();
            if (!h_.done()) {
               h_.resume();
            }
            if (h_.done()) {
               // At the end, propagate any exception and clear the handle
               rethrow_if_failed_();
               h_ = nullptr;
            }
            return *this;
         }
         void operator++(int) { (void)++(*this); }

      private:
         void advance_() {
            if (!h_) return;
            if (!h_.done()) {
               h_.resume();
            }
            if (h_.done()) {
               rethrow_if_failed_();
               h_ = nullptr;
            }
         }

         void rethrow_if_failed_() const {
            if (!h_) return;
            if (h_.promise().error_) std::rethrow_exception(h_.promise().error_);
         }

      private:
         handle_type h_{};
      };

      // ——— Range-Facade ————————————————————————————————————————————————
      iterator begin() {
         return iterator{ h_ };
      }
      std::default_sentinel_t end() const noexcept {
         return {};
      }

      // Manuelles Ziehen (optional)
      bool next(ty& out) {
         if (!h_) return false;
         auto& p = h_.promise();
         p.current_.reset();
         if (!h_.done()) h_.resume();
         if (h_.done()) {
            if (p.error_) std::rethrow_exception(p.error_);
            h_ = nullptr;
            return false;
         }
         if (p.error_) std::rethrow_exception(p.error_);
         out = std::move(*(p.current_));
         return true;
      }

   private:
      void destroy_() noexcept {
         if (h_) {
            h_.destroy();
            h_ = nullptr;
         }
      }

   private:
      handle_type h_{};
   };


   template <class ty>
   class StoppableGenerator {
   public:
      struct promise_type {
         using handle_type = std::coroutine_handle<promise_type>;

         std::optional<ty>   current_;
         std::exception_ptr error_;
         bool               started_{ false };

         StoppableGenerator get_return_object() noexcept {
            return StoppableGenerator{ handle_type::from_promise(*this) };
         }
         std::suspend_always initial_suspend() const noexcept { return {}; }
         std::suspend_always final_suspend()   const noexcept { return {}; }
         void return_void() noexcept {}
         void unhandled_exception() noexcept { error_ = std::current_exception(); }

         template<class U>
            requires std::constructible_from<ty, U&&>
         std::suspend_always yield_value(U&& v) noexcept(std::is_nothrow_constructible_v<ty, U&&>) {
            current_.reset();
            current_.emplace(std::forward<U>(v));
            return {};
         }

         void rethrow_if_error() {
            if (error_) {
               auto e = std::exchange(error_, {});
               std::rethrow_exception(e);
            }
         }
      };

      using handle_type = std::coroutine_handle<promise_type>;

      class iterator {
      public:
         using iterator_concept = std::input_iterator_tag;
         using difference_type = std::ptrdiff_t;
         using value_type = ty;

         iterator() noexcept = default;
         explicit iterator(handle_type h) : h_(h) { advance_(); }

         value_type const& operator*() const {
            check_ok_();
            auto const& opt = h_.promise().current_;
            if (!opt) throw std::logic_error("Generator: deref without current value");
            return *opt;
         }
         value_type const* operator->() const {
            return std::addressof(operator*());
         }

         iterator& operator++() {
            check_ok_();
            h_.promise().current_.reset();
            if (!h_.done())
               h_.resume();
            if (h_.done()) {
               h_.promise().rethrow_if_error();
               h_ = nullptr;
            }
            return *this;
         }
         void operator++(int) { (void)++(*this); }

         friend bool operator==(iterator const& it, std::default_sentinel_t) noexcept {
            return !it.h_;
         }

      private:
         void advance_() {
            if (!h_) return;
            if (!h_.promise().started_) {
               h_.promise().started_ = true;
               if (!h_.done())
                  h_.resume();
               if (h_.done()) {
                  // Completed immediately at the beginning (or an exception occurred)
                  h_.promise().rethrow_if_error();
                  h_ = nullptr;
               }
            }
         }
         void check_ok_() const {
            if (!h_) throw std::logic_error("Generator: invalid iterator");
            // Propagate an exception only when the value is accessed
            if (h_.promise().error_) const_cast<promise_type&>(h_.promise()).rethrow_if_error();
         }

         handle_type h_{};
      };

   public:
      StoppableGenerator() noexcept = default;
      explicit StoppableGenerator(handle_type h) noexcept : h_(h) {}

      StoppableGenerator(StoppableGenerator&& other) noexcept
         : h_(std::exchange(other.h_, {})) {
      }
      StoppableGenerator& operator=(StoppableGenerator&& other) noexcept {
         if (this != &other) {
            destroy_();
            h_ = std::exchange(other.h_, {});
         }
         return *this;
      }

      StoppableGenerator(StoppableGenerator const&) = delete;
      StoppableGenerator& operator=(StoppableGenerator const&) = delete;

      ~StoppableGenerator() { destroy_(); }

      iterator begin() { return iterator{ h_ }; }
      std::default_sentinel_t end() const noexcept { return {}; }

      bool hasFrame() const noexcept { return static_cast<bool>(h_); }

   private:
      void destroy_() noexcept {
         if (h_) {
            // Do not resume again; just release the coroutine
            h_.destroy();
            h_ = nullptr;
         }
      }

      handle_type h_{};
   };



   /**
   \brief Convenience alias
   */
   template <class ty>
   using Generator = SimpleGenerator<ty>;


} // namespace adecc

