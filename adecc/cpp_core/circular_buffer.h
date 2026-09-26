// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file circular_buffer.h
\brief Fixed-capacity circular buffer with logical-order range access.

\details
Provides bounded storage with copy and move insertion, logical front/back access, and a random-access
view over retained elements. The container is suitable for histories and diagnostic trails where memory
growth must remain controlled and iteration must follow logical rather than physical order.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Typed Runtime Structure and RAII".
- "Ranges as a Universal Data Model".
- "The Cost Model of Abstraction".
- "Efficiency as Responsibility".

\see ARCHITECTURE.md#cost-model-and-efficiency

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

#include <array>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

/// \brief Fixed-capacity circular buffer with a view and a safe iterator
/// \details Stores up to SIZE elements of type ty in a std::array.
///          \n The logical order starts at \c iStart_ and contains \c iSize_ elements.
///          \n The view exposes the logical order as a random-access range.
template <typename ty, std::size_t SIZE>
class CircularBuffer : private std::array<ty, SIZE> {
public:
   using base_type  = std::array<ty, SIZE>;
   using value_type = ty;
   using size_type  = std::size_t;
   using reference       = value_type&;
   using const_reference = value_type const&;

private:
   size_type iStart_{0};
   size_type iSize_{0};

   [[nodiscard]] static constexpr size_type wrap_(size_type const i) noexcept {
      static_assert(SIZE > 0, "SIZE must be > 0");
      return i % SIZE;
      }

   [[nodiscard]] constexpr size_type physIndex_(size_type const logical) const noexcept {
      return wrap_(iStart_ + logical);
      }

public:
   using base_type::operator[];

   /// \brief Standardkonstruktor
   CircularBuffer() = default;

   /// \brief Copy constructor
   CircularBuffer(CircularBuffer const& other)
      : base_type(static_cast<base_type const&>(other)),
        iStart_{other.iStart_}, iSize_{other.iSize_} { }

   /// \brief Move constructor
   CircularBuffer(CircularBuffer&& other) noexcept
      : base_type(std::move(static_cast<base_type&>(other))),
        iStart_{std::exchange(other.iStart_, 0u)},
        iSize_{std::exchange(other.iSize_,  0u)} { }

   /// \brief Initializer-List
   CircularBuffer(std::initializer_list<ty> items) {
      for (ty const& theVal : items) push(theVal);
      }

   CircularBuffer& operator=(CircularBuffer const& other) {
      if (this != &other) {
         static_cast<base_type&>(*this) = static_cast<base_type const&>(other);
         iStart_ = other.iStart_;
         iSize_  = other.iSize_;
         }
      return *this;
      }

   CircularBuffer& operator=(CircularBuffer&& other) noexcept {
      if (this != &other) {
         static_cast<base_type&>(*this) = std::move(static_cast<base_type&>(other));
         iStart_ = std::exchange(other.iStart_, 0u);
         iSize_  = std::exchange(other.iSize_,  0u);
         }
      return *this;
      }

   friend void swap(CircularBuffer& a, CircularBuffer& b) noexcept {
      using std::swap;
      swap(static_cast<base_type&>(a), static_cast<base_type&>(b));
      swap(a.iStart_, b.iStart_);
      swap(a.iSize_,  b.iSize_);
      }

   // ------------------------------------------------------------------
   // Capacity and state
   // ------------------------------------------------------------------

   /// \brief Current number of logically stored elements
   [[nodiscard]] constexpr size_type size() const noexcept { return iSize_; }

   /// \brief Maximum capacity
   [[nodiscard]] static constexpr size_type capacity() noexcept { return SIZE; }

   /// \brief true if no elements are stored
   [[nodiscard]] constexpr bool empty() const noexcept { return iSize_ == 0; }

   /// \brief true if the buffer is full
   [[nodiscard]] constexpr bool full() const noexcept { return iSize_ == SIZE; }

   /// \brief Clears the logical content without reinitializing the storage
   void clear() noexcept { iStart_ = 0; iSize_ = 0; }

   // ------------------------------------------------------------------
   // Zugriff
   // ------------------------------------------------------------------

   /// \brief Logical access to element i (0..size()-1)
   /// \throw std::out_of_range bei i >= size()
   reference at(size_type const i) {
      if (i >= iSize_) throw std::out_of_range{"CircularBuffer::at: index"};
      return (*this)[physIndex_(i)];
      }
	  
   /// \copydoc at
   const_reference at(size_type const i) const {
      if (i >= iSize_) throw std::out_of_range{"CircularBuffer::at: index"};
      return (*this)[physIndex_(i)];
      }

   /// \brief First element
   /// \throw std::out_of_range bei leerem Puffer
   reference front() {
      if (empty()) throw std::out_of_range{"CircularBuffer::front: empty"};
      return (*this)[iStart_];
      }
	  
   /// \copydoc front
   const_reference front() const {
      if (empty()) throw std::out_of_range{"CircularBuffer::front: empty"};
      return (*this)[iStart_];
      }

   /// \brief Last element (most recently inserted)
   /// \throw std::out_of_range bei leerem Puffer
   reference back() {
      if (empty()) throw std::out_of_range{"CircularBuffer::back: empty"};
      return (*this)[physIndex_(iSize_ - 1)];
      }
	  
   /// \copydoc back
   const_reference back() const {
      if (empty()) throw std::out_of_range{"CircularBuffer::back: empty"};
      return (*this)[physIndex_(iSize_ - 1)];
      }

   // ------------------------------------------------------------------
   // Modifikation
   // ------------------------------------------------------------------

   /// \brief Appends an element by copy
   void push(value_type const& theVal) {
      (*this)[physIndex_(iSize_)] = theVal;
      if (iSize_ < SIZE) ++iSize_; else iStart_ = wrap_(iStart_ + 1);
      }

   /// \brief Appends an element by move
   void push(value_type&& theVal) {
      (*this)[physIndex_(iSize_)] = std::move(theVal);
      if (iSize_ < SIZE) ++iSize_; else iStart_ = wrap_(iStart_ + 1);
      }

   /// \brief In-place-Konstruktion am Ende
   template <class... Args>
   reference emplace(Args&&... args) {
      size_type const pos = physIndex_(iSize_);
      (*this)[pos] = value_type(std::forward<Args>(args)...);
      if (iSize_ < SIZE) ++iSize_; else iStart_ = wrap_(iStart_ + 1);
      return (*this)[physIndex_(iSize_ ? (iSize_ - 1) : 0)];
      }

   // ------------------------------------------------------------------
   // Range view and iterators (random access, sized)
   // ------------------------------------------------------------------

   /// \brief Internal iterator over the logical order
   template <bool IsConst>
   class Iter {
   public:
      using difference_type   = std::ptrdiff_t;
      using value_type        = ty;
      using iterator_concept  = std::random_access_iterator_tag;
      using iterator_category = std::random_access_iterator_tag;
      using buffer_type       = std::conditional_t<IsConst, CircularBuffer const, CircularBuffer>;
      using reference         = std::conditional_t<IsConst, const_reference, reference>;
      using pointer           = std::conditional_t<IsConst, value_type const*, value_type*>;

      Iter() = default;
      Iter(buffer_type* pBuf, size_type iLogical) : pBuf_{pBuf}, iLogical_{iLogical} { }

      // Dereferenzierung
      reference operator*() const {
         assert(pBuf_ && iLogical_ < pBuf_->iSize_);
         return (*pBuf_)[pBuf_->physIndex_(iLogical_)];
         }
		 
      pointer operator->() const { return std::addressof(operator*()); }

      // Random-Access
      reference operator[](difference_type const n) const {
         auto const i = static_cast<size_type>(static_cast<difference_type>(iLogical_) + n);
         assert(pBuf_ && i < pBuf_->iSize_);
         return (*pBuf_)[pBuf_->physIndex_(i)];
         }

      Iter& operator++()    { ++iLogical_; return *this; }

      Iter  operator++(int) { Iter tmp{*this}; ++*this; return tmp; }

      Iter& operator--()    { --iLogical_; return *this; }

      Iter  operator--(int) { Iter tmp{*this}; --*this; return tmp; }

      Iter& operator+=(difference_type const n) { iLogical_ = static_cast<size_type>(static_cast<difference_type>(iLogical_) + n); return *this; }

      Iter& operator-=(difference_type const n) { iLogical_ = static_cast<size_type>(static_cast<difference_type>(iLogical_) - n); return *this; }

      friend Iter operator+(Iter it, difference_type const n) { it += n; return it; }
	  
      friend Iter operator+(difference_type const n, Iter it) { it += n; return it; }
	  
      friend Iter operator-(Iter it, difference_type const n) { it -= n; return it; }

      friend difference_type operator-(Iter a, Iter b) {
         return static_cast<difference_type>(a.iLogical_) - static_cast<difference_type>(b.iLogical_);
         }

      friend bool operator==(Iter a, Iter b) { return a.pBuf_ == b.pBuf_ && a.iLogical_ == b.iLogical_; }
	  
      friend auto operator<=>(Iter a, Iter b) {
         if (a.pBuf_ != b.pBuf_) return (a.pBuf_ < b.pBuf_) ? std::strong_ordering::less : std::strong_ordering::greater;
         return a.iLogical_ <=> b.iLogical_;
      }

   private:
      buffer_type* pBuf_   = nullptr;
      size_type    iLogical_{0};   // 0..size()
   };

   using iterator       = Iter<false>;
   using const_iterator = Iter<true>;

   /// \brief View exposing the buffer as a ranges-compatible sequence
   class View : public std::ranges::view_interface<View> {
   public:
      using size_type = CircularBuffer::size_type;

      View() = default;
      explicit View(CircularBuffer* p) : pBuf_{p} { }
      explicit View(CircularBuffer const* p) : pBuf_{const_cast<CircularBuffer*>(p)} { }

      [[nodiscard]] iterator begin() {
         return iterator{ pBuf_, 0 };
         }
		 
      [[nodiscard]] iterator end() {
         return iterator{ pBuf_, pBuf_->iSize_ };
         }

      [[nodiscard]] const_iterator begin() const {
         return const_iterator{ pBuf_, 0 };
         }
		 
      [[nodiscard]] const_iterator end() const {
         return const_iterator{ pBuf_, pBuf_->iSize_ };
         }

      [[nodiscard]] size_type size() const noexcept {
         return pBuf_ ? pBuf_->iSize_ : 0u;
         }

      // Optional reverse/cbegin/cend support comes from view_interface via const begin/end

   private:
      CircularBuffer* pBuf_ = nullptr; // non-owning
   };

   /// \brief Creates a view over the logical order
   [[nodiscard]] View view() { return View{ this }; }
   /// \copydoc view
   [[nodiscard]] View view() const { return View{ this }; }
};