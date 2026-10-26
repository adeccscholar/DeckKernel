// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
/**
 \file artwork_recognizer.h
 \brief Independent read-only camera artwork matching against downloaded Scryfall art crops.
*/
#pragma once
#include <opencv2/core.hpp>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace deckkernel::camera {
class ArtworkRecognizer final {
public:
   explicit ArtworkRecognizer(unsigned limit);
   ~ArtworkRecognizer();
   ArtworkRecognizer(ArtworkRecognizer const&)=delete;
   ArtworkRecognizer& operator=(ArtworkRecognizer const&)=delete;
   void Submit(unsigned cardNumber,cv::Mat const& rectified);
   void Cancel() noexcept;
private:
   struct Job {unsigned cardNumber;cv::Mat card;};
   std::mutex m_mutex;
   std::condition_variable m_signal;
   std::optional<Job> m_pending;
   std::atomic<unsigned> m_activeCard{0};
   unsigned m_limit{};
   std::thread m_thread;
   bool m_stopping{};
   void Run() noexcept;
};
}
