/***************************************************************************
 * Unit tests for scrfd_decode_candidates (scrfd_decode.h). Pure logic, no
 * ONNX Runtime, no OpenCV, no model. Build+run:
 *   c++ -std=c++17 -Wall -Wextra test_scrfd_decode.cpp -o t && ./t
 *
 * These exist because face detection returned zero results for every image at
 * every threshold from v2026.5.3 through v2026.8.1: the stride guard compared
 * a ROW count against an ELEMENT count (expected * 4 for bbox, * 10 for kps),
 * which is true for every stride, so all three groups were skipped. Inference
 * ran normally throughout, and the models are not in the repository, so
 * nothing in CI could see it. The first test below fails on that bug.
 *
 * Copyright (c) 2026, Randy Hollines
 ***************************************************************************/

#include "../scrfd_decode.h"

#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while(0)

namespace {

constexpr int INPUT_W = 640;
constexpr int INPUT_H = 640;
constexpr int ANCHORS = 2;

long rows_for(int stride) {
   return (long)(INPUT_H / stride) * (INPUT_W / stride) * ANCHORS;
}

// One stride's worth of backing storage, shaped exactly as det_10g.onnx emits.
struct StrideBuffers {
   std::vector<float> score, bbox, kps;
   explicit StrideBuffers(int stride)
     : score((size_t)rows_for(stride), 0.0f),
       bbox((size_t)rows_for(stride) * 4, 1.0f),
       kps((size_t)rows_for(stride) * 10, 0.0f) {}
};

void append(std::vector<ScrfdTensor>& out, StrideBuffers& b, int stride) {
   out.push_back({b.score.data(), rows_for(stride), 1});
   out.push_back({b.bbox.data(),  rows_for(stride), 4});
   out.push_back({b.kps.data(),   rows_for(stride), 10});
}

} // namespace

int main() {
   const ScrfdPreproc unit;   // scale 1, no padding: network coords == image coords

   // A confident anchor on the stride-8 feature map must survive decoding.
   // This is the regression guard: with the row/element mix-up every stride
   // group was skipped and this returned nothing.
   {
      StrideBuffers s8(8), s16(16), s32(32);
      s8.score[0] = 0.99f;

      std::vector<ScrfdTensor> tensors;
      append(tensors, s8, 8);
      append(tensors, s16, 16);
      append(tensors, s32, 32);

      const std::vector<FaceDet> got = scrfd_decode_candidates(tensors, INPUT_W, INPUT_H, 0.5f, unit);
      CHECK(got.size() == 1, "a confident stride-8 anchor is decoded");
      if(got.size() == 1) {
         CHECK(got[0].score == 0.99f, "score is carried through");
         // anchor (0,0), stride 8, all bbox distances 1 -> [-8,-8,8,8]
         CHECK(got[0].x1 == -8.0f && got[0].y1 == -8.0f, "bbox top-left decoded from distances");
         CHECK(got[0].x2 ==  8.0f && got[0].y2 ==  8.0f, "bbox bottom-right decoded from distances");
      }
   }

   // Every stride group contributes, not just the first.
   {
      StrideBuffers s8(8), s16(16), s32(32);
      s8.score[0]  = 0.9f;
      s16.score[0] = 0.9f;
      s32.score[0] = 0.9f;

      std::vector<ScrfdTensor> tensors;
      append(tensors, s8, 8);
      append(tensors, s16, 16);
      append(tensors, s32, 32);

      const std::vector<FaceDet> got = scrfd_decode_candidates(tensors, INPUT_W, INPUT_H, 0.5f, unit);
      CHECK(got.size() == 3, "all three stride groups decode");
   }

   // Tensors may arrive in any order: they are matched to strides by row count.
   {
      StrideBuffers s8(8), s16(16), s32(32);
      s8.score[0] = 0.9f;

      std::vector<ScrfdTensor> tensors;
      append(tensors, s32, 32);   // smallest first
      append(tensors, s16, 16);
      append(tensors, s8, 8);

      const std::vector<FaceDet> got = scrfd_decode_candidates(tensors, INPUT_W, INPUT_H, 0.5f, unit);
      CHECK(got.size() == 1, "stride matching is order independent");
   }

   // The confidence threshold is honoured on both sides.
   {
      StrideBuffers s8(8), s16(16), s32(32);
      s8.score[0] = 0.40f;
      s8.score[1] = 0.60f;

      std::vector<ScrfdTensor> tensors;
      append(tensors, s8, 8);
      append(tensors, s16, 16);
      append(tensors, s32, 32);

      CHECK(scrfd_decode_candidates(tensors, INPUT_W, INPUT_H, 0.50f, unit).size() == 1, "threshold filters below");
      CHECK(scrfd_decode_candidates(tensors, INPUT_W, INPUT_H, 0.30f, unit).size() == 2, "threshold admits above");
      CHECK(scrfd_decode_candidates(tensors, INPUT_W, INPUT_H, 0.99f, unit).empty(),     "threshold above all scores");
   }

   // Letterbox geometry is undone: padding removed, then scale divided out.
   {
      StrideBuffers s8(8), s16(16), s32(32);
      s8.score[0] = 0.9f;

      std::vector<ScrfdTensor> tensors;
      append(tensors, s8, 8);
      append(tensors, s16, 16);
      append(tensors, s32, 32);

      ScrfdPreproc geom;
      geom.scale = 2.0f;
      geom.pad_x = 10.0f;
      geom.pad_y = 20.0f;

      const std::vector<FaceDet> got = scrfd_decode_candidates(tensors, INPUT_W, INPUT_H, 0.5f, geom);
      CHECK(got.size() == 1, "detection survives letterbox mapping");
      if(got.size() == 1) {
         // x1 = (0 - 1*8 - 10) / 2 = -9 ; y1 = (0 - 1*8 - 20) / 2 = -14
         CHECK(got[0].x1 == -9.0f,  "pad_x and scale applied to x");
         CHECK(got[0].y1 == -14.0f, "pad_y and scale applied to y");
      }
   }

   // A short tensor must be rejected rather than read past its end.
   {
      StrideBuffers s8(8), s16(16), s32(32);
      s8.score[0] = 0.9f;

      std::vector<ScrfdTensor> tensors;
      append(tensors, s8, 8);
      append(tensors, s16, 16);
      append(tensors, s32, 32);
      tensors[1].rows -= 1;   // bbox for stride 8 is one row short

      const std::vector<FaceDet> got = scrfd_decode_candidates(tensors, INPUT_W, INPUT_H, 0.5f, unit);
      CHECK(got.empty(), "an undersized tensor skips its stride group");
   }

   // Degenerate inputs must not crash or invent detections.
   {
      const std::vector<ScrfdTensor> none;
      CHECK(scrfd_decode_candidates(none, INPUT_W, INPUT_H, 0.5f, unit).empty(), "no tensors -> no detections");

      StrideBuffers s8(8);
      std::vector<ScrfdTensor> partial;
      append(partial, s8, 8);   // scores/bbox/kps for one stride only
      s8.score[0] = 0.9f;
      CHECK(scrfd_decode_candidates(partial, INPUT_W, INPUT_H, 0.5f, unit).size() == 1, "a single stride group is enough");

      std::vector<ScrfdTensor> nulls = {{nullptr, 100, 1}, {nullptr, 100, 4}, {nullptr, 100, 10}};
      CHECK(scrfd_decode_candidates(nulls, INPUT_W, INPUT_H, 0.5f, unit).empty(), "null data is ignored");

      std::vector<ScrfdTensor> tensors;
      append(tensors, s8, 8);
      CHECK(scrfd_decode_candidates(tensors, 0, 0, 0.5f, unit).empty(), "zero input size -> no detections");
   }

   if(failures == 0) {
      std::printf("All SCRFD decode tests passed.\n");
      return 0;
   }
   std::printf("%d SCRFD decode test(s) failed.\n", failures);
   return 1;
}
