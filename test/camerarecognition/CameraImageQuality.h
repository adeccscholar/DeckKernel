// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once
/** \file CameraImageQuality.h
    \brief Exposure and reflection diagnostics for a detected card.
    \details White clipped regions cannot be restored reliably. Enhancements
             are only applied to OCR input, never to saved raw evidence.
*/
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace deckkernel_camera::quality {

struct Metrics {
   double brightness{};
   double clippedPercent{};
   double darkPercent{};
   double sharpness{};
   double score{};
   [[nodiscard]] bool GlareRisk() const noexcept {
      return clippedPercent>=4.0;
   }
   [[nodiscard]] bool LowLight() const noexcept {
      return brightness<78.0 || darkPercent>62.0;
   }
};

/** Sample only card pixels, not the background or 4K camera framebuffer. */
[[nodiscard]] inline Metrics Measure(cv::Mat const& card) {
   if(card.empty())return {};
   cv::Mat sample,bgr,gray,hsv;
   cv::resize(card,sample,cv::Size(240,336),0,0,cv::INTER_AREA);
   if(sample.channels()==4)
      cv::cvtColor(sample,bgr,cv::COLOR_BGRA2BGR);
   else if(sample.channels()==1)
      cv::cvtColor(sample,bgr,cv::COLOR_GRAY2BGR);
   else
      bgr=sample;
   cv::cvtColor(bgr,gray,cv::COLOR_BGR2GRAY);
   cv::cvtColor(bgr,hsv,cv::COLOR_BGR2HSV);
   // Ignore the outer border and the white collector-number margin.
   cv::Rect const central(14,33,212,265);
   cv::Mat const shade=gray(central);
   cv::Mat const color=hsv(central);
   auto const mean=cv::mean(shade);
   int const count=central.width*central.height;
   int dark=0,clipped=0;
   for(int y=0;y<central.height;++y) {
      auto const* g=shade.ptr<unsigned char>(y);
      auto const* h=color.ptr<cv::Vec3b>(y);
      for(int x=0;x<central.width;++x) {
         if(g[x]<35)++dark;
         if(g[x]>=247 && h[x][1]<50)++clipped;
      }
   }
   cv::Mat lap;
   cv::Laplacian(shade,lap,CV_32F);
   cv::Scalar avg,dev;
   cv::meanStdDev(lap,avg,dev);
   Metrics m;
   m.brightness=mean[0];
   m.clippedPercent=100.0*clipped/count;
   m.darkPercent=100.0*dark/count;
   m.sharpness=dev[0]*dev[0];
   // Prefer a sharp unglared frame, not just the sharpest highlight.
   m.score=30.0*std::log1p(m.sharpness)
      -1.6*m.clippedPercent-0.20*m.darkPercent;
   return m;
}

/** Use local-contrast enhancement on a grayscale OCR crop when necessary.
    Do not inpaint highlights: missing glyph pixels are not recoverable.
*/
[[nodiscard]] inline cv::Mat ForOcr(cv::Mat const& crop) {
   cv::Mat gray;
   if(crop.channels()==3)cv::cvtColor(crop,gray,cv::COLOR_BGR2GRAY);
   else if(crop.channels()==4)cv::cvtColor(crop,gray,cv::COLOR_BGRA2GRAY);
   else gray=crop;
   cv::Scalar avg,dev;
   cv::meanStdDev(gray,avg,dev);
   if(avg[0]>=100.0 && dev[0]>=43.0)return gray.clone();
   cv::Mat enhanced;
   auto clahe=cv::createCLAHE(2.0,cv::Size(8,8));
   clahe->apply(gray,enhanced);
   return enhanced;
}
} // namespace deckkernel_camera::quality
