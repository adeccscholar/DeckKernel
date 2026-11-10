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
   double nameSharpness{};
   double typeSharpness{};
   double detailSharpness{};
   double score{};
   int sourceWidth{};
   int sourceHeight{};
   /**
   \brief Test minimum photographic evidence for visual recognition.
   \details These thresholds are provisional engineering safeguards, not
   calibrated recognition probabilities. The actual card must have enough
   source pixels; enlarging an undersampled image does not count.
   \returns True when capture quality is sufficient for image comparison.
   \throw None.
   */
   [[nodiscard]] bool UsableForMatching() const noexcept {
      return sourceWidth>=360 && sourceHeight>=500 &&
         brightness>=65.0 && clippedPercent<12.0 &&
         sharpness>=15.0 && detailSharpness>=18.0;
   }
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
   m.sourceWidth=card.cols;
   m.sourceHeight=card.rows;
   m.brightness=mean[0];
   m.clippedPercent=100.0*clipped/count;
   m.darkPercent=100.0*dark/count;
   m.sharpness=dev[0]*dev[0];

   // PHASE 04: measure printed edge detail on ORIGINAL source pixels, not
   // just Laplacian variance in a downscaled illustration. Printed title
   // and type areas are more relevant to printing identification than art.
   // Downsample only unusually large rectifications to bound CPU work.
   cv::Mat aDetails=card;
   if(card.cols>1120) {
      cv::resize(card,aDetails,cv::Size(),1120.0/card.cols,
         1120.0/card.cols,cv::INTER_AREA);
   }
   cv::Mat aDetailGray;
   if(aDetails.channels()==4)
      cv::cvtColor(aDetails,aDetailGray,cv::COLOR_BGRA2GRAY);
   else if(aDetails.channels()==3)
      cv::cvtColor(aDetails,aDetailGray,cv::COLOR_BGR2GRAY);
   else
      aDetailGray=aDetails;
   auto const DetailSharpness=[&](int iY,int iHeight)->double {
      cv::Rect const aRegion(
         aDetailGray.cols*11/100,aDetailGray.rows*iY/100,
         std::max(1,aDetailGray.cols*78/100),
         std::max(1,aDetailGray.rows*iHeight/100));
      cv::Mat aLaplacian;
      cv::Laplacian(aDetailGray(aRegion),aLaplacian,CV_32F);
      cv::Scalar aAverage,aDeviation;
      cv::meanStdDev(aLaplacian,aAverage,aDeviation);
      return aDeviation[0]*aDeviation[0];
   };
   m.nameSharpness=DetailSharpness(5,10);
   m.typeSharpness=DetailSharpness(54,9);
   m.detailSharpness=std::min(m.nameSharpness,m.typeSharpness);
   // A source with sharp artwork but blurred printing text must lose score.
   m.score=30.0*std::log1p(m.sharpness)
      +18.0*std::log1p(m.detailSharpness)
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
