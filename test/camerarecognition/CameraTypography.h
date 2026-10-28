// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

/** \file CameraTypography.h
    \brief Fallback vector lettering for the camera dashboard, adapted from BuildEngine-Tests WeatherClient.
    \details Does not depend on platform font discovery or bundled fonts.
    Individual glyph cells are collected in one SkPath per text run.
    Lowercase ASCII is rendered as uppercase. Unsupported UTF-8 bytes
    are rendered as a question mark; display strings are English.
*/
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <string_view>

namespace deckkernel_camera::typography {
   [[nodiscard]] constexpr std::array<std::uint8_t,7> Glyph(char ch) noexcept {
      switch(ch) {
         case 'A': return {14,17,17,31,17,17,17};
         case 'B': return {30,17,17,30,17,17,30};
         case 'C': return {14,17,16,16,16,17,14};
         case 'D': return {30,17,17,17,17,17,30};
         case 'E': return {31,16,16,30,16,16,31};
         case 'F': return {31,16,16,30,16,16,16};
         case 'G': return {14,17,16,23,17,17,15};
         case 'H': return {17,17,17,31,17,17,17};
         case 'I': return {14,4,4,4,4,4,14};
         case 'J': return {7,2,2,2,18,18,12};
         case 'K': return {17,18,20,24,20,18,17};
         case 'L': return {16,16,16,16,16,16,31};
         case 'M': return {17,27,21,21,17,17,17};
         case 'N': return {17,25,21,19,17,17,17};
         case 'O': return {14,17,17,17,17,17,14};
         case 'P': return {30,17,17,30,16,16,16};
         case 'Q': return {14,17,17,17,21,18,13};
         case 'R': return {30,17,17,30,20,18,17};
         case 'S': return {15,16,16,14,1,1,30};
         case 'T': return {31,4,4,4,4,4,4};
         case 'U': return {17,17,17,17,17,17,14};
         case 'V': return {17,17,17,17,17,10,4};
         case 'W': return {17,17,17,21,21,21,10};
         case 'X': return {17,17,10,4,10,17,17};
         case 'Y': return {17,17,10,4,4,4,4};
         case 'Z': return {31,1,2,4,8,16,31};
         case '0': return {14,17,19,21,25,17,14};
         case '1': return {4,12,4,4,4,4,14};
         case '2': return {14,17,1,2,4,8,31};
         case '3': return {30,1,1,14,1,1,30};
         case '4': return {2,6,10,18,31,2,2};
         case '5': return {31,16,16,30,1,1,30};
         case '6': return {14,16,16,30,17,17,14};
         case '7': return {31,1,2,4,8,8,8};
         case '8': return {14,17,17,14,17,17,14};
         case '9': return {14,17,17,15,1,1,14};
         case '.': return {0,0,0,0,0,12,12};
         case ',': return {0,0,0,0,4,4,8};
         case ':': return {0,4,4,0,4,4,0};
         case ';': return {0,4,4,0,4,4,8};
         case '-': return {0,0,0,31,0,0,0};
         case '_': return {0,0,0,0,0,0,31};
         case '/': return {1,1,2,4,8,16,16};
         case '+': return {0,4,4,31,4,4,0};
         case '=': return {0,0,31,0,31,0,0};
         case '%': return {17,18,2,4,8,9,17};
         case '!': return {4,4,4,4,4,0,4};
         case '?': return {14,17,1,2,4,0,4};
         case '#': return {10,10,31,10,31,10,10};
         case '(': return {2,4,8,8,8,4,2};
         case ')': return {8,4,2,2,2,4,8};
         case '[': return {14,8,8,8,8,8,14};
         case ']': return {14,2,2,2,2,2,14};
         case '<': return {2,4,8,16,8,4,2};
         case '>': return {8,4,2,1,2,4,8};
         case '|': return {4,4,4,4,4,4,4};
         case '\'': return {4,4,8,0,0,0,0};
         case '"': return {10,10,0,0,0,0,0};
         case '*': return {0,21,14,31,14,21,0};
         case ' ': return {0,0,0,0,0,0,0};
         default: return {14,17,2,4,4,0,4};
      }
   }

   inline void Draw(SkCanvas* canvas,float x,float baseline,
      std::string_view text,float size,SkColor color) {
      if(text.empty()||size<=0) return;
      float const cell=(std::max)(0.75f,size/8.4f);
      SkPathBuilder builder;
      float left=x;
      for(unsigned char const byte:text) {
         char ch=static_cast<char>(byte);
         if(ch>='a'&&ch<='z') ch=static_cast<char>(ch-'a'+'A');
         auto const glyph=Glyph(ch);
         for(int row=0;row<7;++row) {
            for(int col=0;col<5;++col) {
               if((glyph[static_cast<std::size_t>(row)]&(1U<<(4-col)))==0)continue;
               float const gx=left+col*cell;
               float const gy=baseline-7*cell+row*cell;
               builder.addRect(SkRect::MakeXYWH(gx,gy,cell*.82f,cell*.84f));
            }
         }
         left+=cell*6.f;
      }
      SkPaint paint;
      paint.setAntiAlias(true);
      paint.setColor(color);
      canvas->drawPath(builder.detach(),paint);
   }
} // namespace weather_client::typography
