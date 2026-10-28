// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once
/** \file CameraOcrZones.h
    \brief Separate OCR text from Magic card symbols in rectified modern frames.
    \details Ratios refer to the perspective-corrected portrait card (480x672).
             This is a modern-frame profile, not a general layout classifier.
*/
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace deckkernel_camera::ocr {
struct Zones {
   cv::Rect name;
   cv::Rect mana;
   cv::Rect type;
   cv::Rect setSymbol;
   cv::Rect rules;
};

[[nodiscard]] inline cv::Rect Relative(cv::Size size,
   int x0,int y0,int x1,int y1) {
   auto x=[&](int p){return std::clamp(size.width*p/1000,0,size.width);};
   auto y=[&](int p){return std::clamp(size.height*p/1000,0,size.height);};
   return {x(x0),y(y0),std::max(1,x(x1)-x(x0)),
      std::max(1,y(y1)-y(y0))};
}

[[nodiscard]] inline Zones Detect(cv::Mat const& card) {
   if(card.empty() || card.cols<100 || card.rows<200)
      throw std::invalid_argument("rectified card is too small for OCR zones");
   // Strict non-overlapping text/symbol boundaries:
   //  - title text ends before the right-hand mana-cost symbols
   //  - typeline text ends before the edition/set symbol
   //  - rules start below the typeline; never include type or edition.
   return {
      Relative(card.size(),  54,  35, 745, 133), // name only
      Relative(card.size(), 745,  35, 964, 133), // mana/cost only
      Relative(card.size(),  57, 520, 837, 600), // type + supertypes
      Relative(card.size(), 837, 520, 964, 600), // set symbol only
      Relative(card.size(),  70, 610, 930, 846)  // rules/flavour text
   };
}

[[nodiscard]] inline bool Valid(Zones const& z,cv::Size size) {
   auto inside=[&](cv::Rect r) {
      return r.x>=0&&r.y>=0&&r.width>0&&r.height>0&&
         r.x+r.width<=size.width&&r.y+r.height<=size.height;
   };
   return inside(z.name)&&inside(z.mana)&&inside(z.type)&&
      inside(z.setSymbol)&&inside(z.rules)&&
      (z.name&z.mana).empty()&&(z.type&z.setSymbol).empty()&&
      (z.type&z.rules).empty()&&(z.setSymbol&z.rules).empty();
}

[[nodiscard]] inline cv::Mat Diagram(cv::Mat const& card,Zones const& z) {
   cv::Mat annotated=card.clone();
   auto draw=[&](cv::Rect box,cv::Scalar color,char const* label) {
      cv::rectangle(annotated,box,color,2,cv::LINE_AA);
      cv::putText(annotated,label,cv::Point(box.x+3,std::max(14,box.y-3)),
         cv::FONT_HERSHEY_SIMPLEX,0.38,color,1,cv::LINE_AA);
   };
   draw(z.name,cv::Scalar(40,220,40),"NAME");
   draw(z.mana,cv::Scalar(230,180,40),"MANA");
   draw(z.type,cv::Scalar(20,230,230),"TYPE");
   draw(z.setSymbol,cv::Scalar(230,70,230),"SET");
   draw(z.rules,cv::Scalar(40,140,240),"RULES");
   return annotated;
}
}
