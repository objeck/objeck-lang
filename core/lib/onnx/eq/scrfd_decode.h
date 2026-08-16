/***************************************************************************
 * SCRFD detection decode -- anchor decoding for the InsightFace SCRFD face
 * detector, split out of common.h so it can be unit tested.
 *
 * Deliberately free of ONNX Runtime and OpenCV: it takes plain tensor
 * descriptors and returns pre-NMS candidates, so a test can feed it synthetic
 * outputs with no model, no network and no GPU. This mirrors phi3_sampling.h.
 *
 * The split exists because a guard here compared a ROW count against an
 * ELEMENT count and silently skipped every stride group, so face detection
 * returned zero results for every image at every threshold from v2026.5.3
 * through v2026.8.1 while inference itself ran normally. Nothing in CI could
 * observe that -- the models are not in the repository.
 *
 * Copyright (c) 2026, Randy Hollines
 ***************************************************************************/

#ifndef __SCRFD_DECODE_H__
#define __SCRFD_DECODE_H__

#include <algorithm>
#include <vector>

// A single face detection, in ORIGINAL image coordinates.
struct FaceDet {
   float x1, y1, x2, y2;
   float score;
   float kps[10];   // 5 landmarks: x0,y0,x1,y1,...x4,y4
};

// One SCRFD output tensor, shaped [rows, cols]. cols identifies its role:
// 1 = score, 4 = bbox distances, 10 = keypoint offsets.
struct ScrfdTensor {
   const float* data;
   long rows;
   long cols;
};

// Letterbox geometry needed to map network coordinates back onto the image.
struct ScrfdPreproc {
   float scale = 1.0f;
   float pad_x = 0.0f;
   float pad_y = 0.0f;
};

// Decode SCRFD outputs into candidate detections. NMS is the caller's job.
//
// det_10g.onnx emits 9 tensors -- 3x score [N,1], 3x bbox [N,4], 3x kps [N,10]
// -- grouped by stride (8, 16, 32) with 2 anchors per grid cell. Tensors are
// matched to strides by row count descending, since stride 8 has the most rows.
inline std::vector<FaceDet> scrfd_decode_candidates(
   const std::vector<ScrfdTensor>& tensors,
   int input_w, int input_h,
   float conf_threshold,
   const ScrfdPreproc& pp)
{
   std::vector<FaceDet> candidates;
   if(input_w <= 0 || input_h <= 0) {
      return candidates;
   }

   std::vector<ScrfdTensor> sc_list, bb_list, kp_list;
   for(const ScrfdTensor& t : tensors) {
      if(t.data == nullptr || t.rows <= 0) {
         continue;
      }
      if(t.cols == 1)       sc_list.push_back(t);
      else if(t.cols == 4)  bb_list.push_back(t);
      else if(t.cols == 10) kp_list.push_back(t);
   }

   const auto by_rows_desc = [](const ScrfdTensor& a, const ScrfdTensor& b) { return a.rows > b.rows; };
   std::sort(sc_list.begin(), sc_list.end(), by_rows_desc);
   std::sort(bb_list.begin(), bb_list.end(), by_rows_desc);
   std::sort(kp_list.begin(), kp_list.end(), by_rows_desc);

   const int strides[3] = {8, 16, 32};
   const int groups = (int)std::min({sc_list.size(), bb_list.size(), kp_list.size(), (size_t)3});

   for(int si = 0; si < groups; ++si) {
      const int stride  = strides[si];
      const int grid_h  = input_h / stride;
      const int grid_w  = input_w / stride;
      const int anchors = 2;

      // Every tensor is [rows, cols] and 'rows' is already the anchor count, so
      // all three are compared against 'expected' directly. Scaling by cols --
      // expected * 4 for bbox, * 10 for kps -- makes this true for every stride,
      // skipping all of them and yielding zero faces at any threshold.
      const long expected = (long)grid_h * grid_w * anchors;
      if(sc_list[si].rows < expected || bb_list[si].rows < expected || kp_list[si].rows < expected) {
         continue;
      }

      const float* sc = sc_list[si].data;
      const float* bb = bb_list[si].data;
      const float* kp = kp_list[si].data;

      int idx = 0;
      for(int y = 0; y < grid_h; ++y) {
         for(int x = 0; x < grid_w; ++x) {
            for(int a = 0; a < anchors; ++a, ++idx) {
               const float score = sc[idx];
               if(score < conf_threshold) {
                  continue;
               }

               const float cx = (float)(x * stride);
               const float cy = (float)(y * stride);

               // distance2bbox: (cx +/- distance*stride), mapped back through the letterbox
               FaceDet det;
               det.x1 = (cx - bb[idx*4+0] * stride - pp.pad_x) / pp.scale;
               det.y1 = (cy - bb[idx*4+1] * stride - pp.pad_y) / pp.scale;
               det.x2 = (cx + bb[idx*4+2] * stride - pp.pad_x) / pp.scale;
               det.y2 = (cy + bb[idx*4+3] * stride - pp.pad_y) / pp.scale;
               det.score = score;
               for(int k = 0; k < 5; ++k) {
                  det.kps[k*2+0] = (cx + kp[idx*10 + k*2+0] * stride - pp.pad_x) / pp.scale;
                  det.kps[k*2+1] = (cy + kp[idx*10 + k*2+1] * stride - pp.pad_y) / pp.scale;
               }
               candidates.push_back(det);
            }
         }
      }
   }

   return candidates;
}

#endif
