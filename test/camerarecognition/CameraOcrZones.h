// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once
/** \file CameraOcrZones.h
    \brief Image-derived text and symbol zones for rectified MTG cards.
    \details Frame lines are optional evidence; the OCR text-line positions
             can supply the actual typeline bounds. The rules area is always
             available independently, even for unknown layouts.
*/
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace deckkernel_camera::ocr {

struct Zones {
   cv::Rect name;
   cv::Rect mana;
   cv::Rect type;
   cv::Rect setSymbol;
   cv::Rect rules;
   bool typeSeparated{};
   float separationConfidence{};
};

[[nodiscard]] inline cv::Rect Relative(cv::Size size,
   int x0,int y0,int x1,int y1) {
   auto x=[&](int p){return std::clamp(size.width*p/1000,0,size.width);};
   auto y=[&](int p){return std::clamp(size.height*p/1000,0,size.height);};
   return {x(x0),y(y0),std::max(1,x(x1)-x(x0)),
      std::max(1,y(y1)-y(y0))};
}

/** A second check on the detected type bar, not a substitute for geometry.
    Names, symbols and decorative text must not be treated as card types.
*/
[[nodiscard]] inline bool LooksLikeType(std::string_view text) {
   std::string lower;
   lower.reserve(text.size());
   for(unsigned char c:text)
      lower.push_back(static_cast<char>(std::tolower(c)));
   for(std::string_view const token:{"creature","land","artifact",
      "enchantment","instant","sorcery","planeswalker","battle",
      "summon","enchant","kindred","tribal","vanguard",
      "scheme","dungeon","conspiracy"})
      if(lower.find(token)!=std::string::npos)return true;
   return false;
}

struct Separator {
   int y{};
   float coverage{};
   int first{};
   int last{};
};

/** Detect long horizontal frame boundaries in a wide central image strip.
    Percentage limits only define a broad search area; the actual start of
    rules is determined by the detected lower border of the type bar.
*/
[[nodiscard]] inline std::vector<Separator> HorizontalRules(cv::Mat const& card) {
   cv::Mat gray,edges,lines;
   if(card.channels()==3)
      cv::cvtColor(card,gray,cv::COLOR_BGR2GRAY);
   else if(card.channels()==4)
      cv::cvtColor(card,gray,cv::COLOR_BGRA2GRAY);
   else
      gray=card;
   cv::Canny(gray,edges,60,150);
   int const left=card.cols*75/1000;
   int const right=card.cols*925/1000;
   cv::Rect const interior(left,0,right-left,card.rows);
   cv::Mat const core=edges(interior);
   cv::morphologyEx(core,lines,cv::MORPH_CLOSE,
      cv::getStructuringElement(cv::MORPH_RECT,
         cv::Size(std::max(3,card.cols/65),1)));
   cv::morphologyEx(lines,lines,cv::MORPH_OPEN,
      cv::getStructuringElement(cv::MORPH_RECT,
         cv::Size(std::max(10,card.cols/10),1)));
   std::vector<Separator> peaks;
   int const searchStart=card.rows*30/100;
   int const searchEnd=card.rows*82/100;
   int lastAccepted=-100;
   for(int y=searchStart;y<searchEnd;++y) {
      float const coverage=static_cast<float>(cv::countNonZero(lines.row(y)))/
         static_cast<float>(interior.width);
      if(coverage<0.40f)continue;
      // Thick/double strokes of the same physical border form one separator.
      if(!peaks.empty() && y-lastAccepted<=7) {
         peaks.back().last=y;
         if(coverage>peaks.back().coverage) {
            peaks.back().y=y;
            peaks.back().coverage=coverage;
         }
      } else {
         peaks.push_back({y,coverage,y,y});
      }
      lastAccepted=y;
   }
   return peaks;
}

/** Locate the circular mana-cost cluster in the name bar. A group of mana
    symbols may extend far to the left; its leftmost boundary, not a fixed
    title width, determines where name OCR must stop. When no cluster can be
    trusted, use a conservative name width rather than OCR the symbols.
*/
[[nodiscard]] inline int ManaLeft(cv::Mat const& card) {
   int const fallback=card.cols*64/100;
   cv::Rect const area=Relative(card.size(),520,28,967,145);
   cv::Mat gray,filtered;
   cv::cvtColor(card(area),gray,
      card.channels()==4?cv::COLOR_BGRA2GRAY:cv::COLOR_BGR2GRAY);
   cv::GaussianBlur(gray,filtered,cv::Size(5,5),1.3);
   std::vector<cv::Vec3f> circles;
   cv::HoughCircles(filtered,circles,cv::HOUGH_GRADIENT,1.2,
      static_cast<double>(std::max(12,card.cols/30)),
      110,17,
      std::max(6,card.cols/85),std::max(14,card.cols/25));
   struct Circle {int x{},y{},radius{};};
   std::vector<Circle> candidates;
   for(auto const& circle:circles) {
      int const x=area.x+cvRound(circle[0]);
      int const y=area.y+cvRound(circle[1]);
      int const radius=cvRound(circle[2]);
      if(x<card.cols*55/100 || y<card.rows*35/1000 ||
         y>card.rows*138/1000)continue;
      candidates.push_back({x,y,radius});
   }
   // Mana clusters are right-aligned: start at the rightmost symbol and
   // accept neighboring circles only if they share the same title baseline.
   auto rightmost=std::max_element(candidates.begin(),candidates.end(),
      [](Circle const& a,Circle const& b){return a.x<b.x;});
   if(rightmost==candidates.end()||rightmost->x<card.cols*80/100)
      return fallback;
   int left=rightmost->x-rightmost->radius;
   int previous=rightmost->x;
   int const row=rightmost->y;
   std::vector<bool> used(candidates.size(),false);
   for(;;) {
      int nearest=-1;
      int nearestX=-1;
      for(std::size_t i=0;i<candidates.size();++i) {
         auto const& c=candidates[i];
         if(used[i] || c.x>=previous || c.x<previous-card.cols*9/100 ||
            std::abs(c.y-row)>std::max(9,card.rows/35))continue;
         if(c.x>nearestX){nearest=static_cast<int>(i);nearestX=c.x;}
      }
      if(nearest<0)break;
      used[static_cast<std::size_t>(nearest)]=true;
      auto const& c=candidates[static_cast<std::size_t>(nearest)];
      previous=c.x;
      left=std::min(left,c.x-c.radius);
   }
   return std::clamp(left-std::max(5,card.cols/80),
      card.cols*54/100,card.cols*88/100);
}

/** Find both borders of the typeline; never return overlapping OCR regions.
    The type bar position and height may vary among card layouts.
*/
[[nodiscard]] inline Zones Detect(cv::Mat const& card) {
   if(card.empty()||card.cols<100||card.rows<200)
      throw std::invalid_argument("rectified card is too small for OCR zones");
   cv::Size const size=card.size();
   int const manaLeft=ManaLeft(card);
   int const titleLeft=size.width*54/1000;
   int const manaRight=size.width*964/1000;
   int const titleRight=std::clamp(manaLeft,titleLeft+50,manaRight-15);
   Zones result {
      cv::Rect(titleLeft,size.height*35/1000,titleRight-titleLeft,
         size.height*133/1000-size.height*35/1000),
      cv::Rect(titleRight,size.height*35/1000,manaRight-titleRight,
         size.height*133/1000-size.height*35/1000),
      Relative(size,  57, 545, 837, 625), // fallback type, no mana/set symbol
      Relative(size, 837, 545, 964, 625),
      Relative(size,  70, 643, 930, 880), // independent rules fallback
      false,0.f
   };
   auto const peaks=HorizontalRules(card);
   float best=-1.f;
   Separator top{},bottom{};
   bool separated=false;
   int const minGap=std::max(18,card.rows*35/1000);
   int const maxGap=std::max(minGap+1,card.rows*105/1000);
   for(auto const& a:peaks) {
      for(auto const& b:peaks) {
         int const gap=b.y-a.y;
         if(gap<minGap||gap>maxGap)continue;
         if(a.y<card.rows*39/100||b.y>card.rows*80/100)continue;
         if(a.coverage<0.52f||b.coverage<0.52f)continue;
         float const ideal=static_cast<float>(card.rows)*0.06f;
         float const widthScore=std::max(0.f,
            1.f-std::abs(static_cast<float>(gap)-ideal)/(ideal+10.f));
         float const score=a.coverage+b.coverage+0.30f*widthScore;
         if(score>best) {
            best=score;
            top=a;
            bottom=b;
            separated=true;
         }
      }
   }
   if(!separated)return result;
   // Skip the border thickness and leave a safety gap after the type bar.
   int const margin=std::max(3,card.rows/180);
   int const typeTop=top.last+margin;
   int const typeBottom=bottom.first-margin;
   int const rulesTop=bottom.last+std::max(8,card.rows/84);
   int const rulesBottom=std::min(card.rows*89/100,
      bottom.last+card.rows*39/100);
   if(typeBottom<=typeTop+8||rulesBottom<=rulesTop+15)
      return result;
   auto band=[&](int x0,int x1,int y0,int y1) {
      int const l=size.width*x0/1000;
      int const r=size.width*x1/1000;
      return cv::Rect(l,y0,r-l,y1-y0);
   };
   result.type=band(57,837,typeTop,typeBottom);
   result.setSymbol=band(837,964,typeTop,typeBottom);
   result.rules=band(70,930,rulesTop,rulesBottom);
   result.typeSeparated=true;
   result.separationConfidence=std::clamp((best-1.f)/1.30f,0.f,1.f);
   return result;
}

/** Refine the zones from an actual OCR text line belonging to the typeline.
    Unlike a fixed card ratio, top/bottom come from Tesseract's bounding box.
    Name and mana remain isolated; the set symbol follows the type band.
*/
inline bool ApplyTypeTextLine(Zones& zones,cv::Size const size,
   int top,int bottom,float confidence) {
   if(top<size.height*42/100||bottom>size.height*77/100||
      bottom<=top||bottom-top>size.height*13/100)
      return false;
   int const pad=std::max(3,size.height/135);
   int const y0=std::max(0,top-pad);
   int const y1=std::min(size.height,bottom+pad);
   int const rulesY=std::min(size.height-2,
      bottom+std::max(10,size.height/60));
   int const rulesEnd=size.height*89/100;
   if(rulesEnd<=rulesY+15)return false;
   zones.type.y=y0;
   zones.type.height=y1-y0;
   zones.setSymbol.y=y0;
   zones.setSymbol.height=y1-y0;
   zones.rules.y=rulesY;
   zones.rules.height=rulesEnd-rulesY;
   zones.typeSeparated=true;
   zones.separationConfidence=std::clamp(confidence/100.f,0.f,1.f);
   return true;
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
   draw(z.type,cv::Scalar(20,230,230),z.typeSeparated?"TYPE":"TYPE?");
   draw(z.setSymbol,cv::Scalar(230,70,230),"SET");
   draw(z.rules,cv::Scalar(40,140,240),z.typeSeparated?"RULES":"RULES?");
   return annotated;
}
} // namespace deckkernel_camera::ocr
