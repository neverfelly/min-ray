#include "bvh.h"
#include <min/visual/shape.h>

namespace min {

// BVHAccel Local Declarations
struct BVHPrimitiveInfo {
  BVHPrimitiveInfo() {}
  BVHPrimitiveInfo(size_t primitiveNumber, const Bounds3f &bounds)
      : primitiveNumber(primitiveNumber),
        bounds(bounds),
        centroid(.5f * bounds.pmin + .5f * bounds.pmax) {}
  size_t primitiveNumber;
  Bounds3f bounds;
  Point3f centroid;
};

struct BVHBuildNode {
  // BVHBuildNode Public Methods
  void InitLeaf(int first, int n, const Bounds3f &b) {
    firstPrimOffset = first;
    nPrimitives = n;
    bounds = b;
    children[0] = children[1] = nullptr;
  }
  void InitInterior(int axis, BVHBuildNode *c0, BVHBuildNode *c1) {
    children[0] = c0;
    children[1] = c1;
    bounds = Union(c0->bounds, c1->bounds);
    splitAxis = axis;
    nPrimitives = 0;
  }
  Bounds3f bounds;
  BVHBuildNode *children[2];
  int splitAxis, firstPrimOffset, nPrimitives;
};

struct MortonPrimitive {
  int primitiveIndex;
  uint32_t mortonCode;
};

struct LBVHTreelet {
  int startIndex, nPrimitives;
  BVHBuildNode *buildNodes;
};

struct LinearBVHNode {
  Bounds3f bounds;
  union {
    int primitivesOffset;   // leaf
    int secondChildOffset;  // interior
  };
  uint16_t nPrimitives;  // 0 -> interior node
  uint8_t axis;          // interior node: xyz
  uint8_t pad[1];        // ensure 32 byte total size
};

// BVHAccel Utility Functions
inline uint32_t LeftShift3(uint32_t x) {
  MIN_ASSERT(x <= (1 << 10));
  if (x == (1 << 10)) --x;
#ifdef HAVE_BINARY_CONSTANTS
  x = (x | (x << 16)) & 0b00000011000000000000000011111111;
    // x = ---- --98 ---- ---- ---- ---- 7654 3210
    x = (x | (x << 8)) & 0b00000011000000001111000000001111;
    // x = ---- --98 ---- ---- 7654 ---- ---- 3210
    x = (x | (x << 4)) & 0b00000011000011000011000011000011;
    // x = ---- --98 ---- 76-- --54 ---- 32-- --10
    x = (x | (x << 2)) & 0b00001001001001001001001001001001;
    // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
#else
  x = (x | (x << 16)) & 0x30000ff;
  // x = ---- --98 ---- ---- ---- ---- 7654 3210
  x = (x | (x << 8)) & 0x300f00f;
  // x = ---- --98 ---- ---- 7654 ---- ---- 3210
  x = (x | (x << 4)) & 0x30c30c3;
  // x = ---- --98 ---- 76-- --54 ---- 32-- --10
  x = (x | (x << 2)) & 0x9249249;
  // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
#endif // HAVE_BINARY_CONSTANTS
  return x;
}

inline uint32_t EncodeMorton3(const Vector3f &v) {
  MIN_ASSERT(v.x >= 0);
  MIN_ASSERT(v.y >= 0);
  MIN_ASSERT(v.z >= 0);
  return (LeftShift3(v.z) << 2) | (LeftShift3(v.y) << 1) | LeftShift3(v.x);
}

static void RadixSort(std::vector<MortonPrimitive> *v) {
  std::vector<MortonPrimitive> tempVector(v->size());
  constexpr int bitsPerPass = 6;
  constexpr int nBits = 30;
  static_assert((nBits % bitsPerPass) == 0,
                "Radix sort bitsPerPass must evenly divide nBits");
  constexpr int nPasses = nBits / bitsPerPass;

  for (int pass = 0; pass < nPasses; ++pass) {
    // Perform one pass of radix sort, sorting _bitsPerPass_ bits
    int lowBit = pass * bitsPerPass;

    // Set in and out vector pointers for radix sort pass
    std::vector<MortonPrimitive> &in = (pass & 1) ? tempVector : *v;
    std::vector<MortonPrimitive> &out = (pass & 1) ? *v : tempVector;

    // Count number of zero bits in array for current radix sort bit
    constexpr int nBuckets = 1 << bitsPerPass;
    int bucketCount[nBuckets] = {0};
    constexpr int bitMask = (1 << bitsPerPass) - 1;
    for (const MortonPrimitive &mp : in) {
      int bucket = (mp.mortonCode >> lowBit) & bitMask;
      MIN_ASSERT(bucket >= 0);
      MIN_ASSERT(bucket < nBuckets);
      ++bucketCount[bucket];
    }

    // Compute starting index in output array for each bucket
    int outIndex[nBuckets];
    outIndex[0] = 0;
    for (int i = 1; i < nBuckets; ++i)
      outIndex[i] = outIndex[i - 1] + bucketCount[i - 1];

    // Store sorted values in output array
    for (const MortonPrimitive &mp : in) {
      int bucket = (mp.mortonCode >> lowBit) & bitMask;
      out[outIndex[bucket]++] = mp;
    }
  }
  // Copy final result from _tempVector_, if needed
  if (nPasses & 1) std::swap(*v, tempVector);
}

struct BucketInfo {
  int count = 0;
  Bounds3f bounds;
};

Bounds3f BVHAccel::WorldBound() const {
  return nodes ? nodes[0].bounds : Bounds3f();;
}

BVHBuildNode *BVHAccel::recursiveBuild(std::vector<BVHPrimitiveInfo> &primitiveInfo,
                                       int start,
                                       int end,
                                       int *totalNodes,
                                       std::vector<std::shared_ptr<Shape>> &orderedPrims) {
  MIN_ASSERT(start != end);
  BVHBuildNode *node = new BVHBuildNode();
  (*totalNodes)++;
  // Compute bounds of all primitives in BVH node
  Bounds3f bounds;
  for (int i = start; i < end; ++i)
    bounds = Union(bounds, primitiveInfo[i].bounds);
  int nPrimitives = end - start;
  if (nPrimitives == 1) {
    // Create leaf _BVHBuildNode_
    int firstPrimOffset = orderedPrims.size();
    for (int i = start; i < end; ++i) {
      int primNum = primitiveInfo[i].primitiveNumber;
      orderedPrims.push_back(primitives[primNum]);
    }
    node->InitLeaf(firstPrimOffset, nPrimitives, bounds);
    return node;
  } else {
    // Compute bound of primitive centroids, choose split dimension _dim_
    Bounds3f centroidBounds;
    for (int i = start; i < end; ++i)
      centroidBounds = Union(centroidBounds, primitiveInfo[i].centroid);
    int dim = centroidBounds.MaximumExtent();

    // Partition primitives into two sets and build children
    int mid = (start + end) / 2;
    if (centroidBounds.pmax[dim] == centroidBounds.pmin[dim]) {
      // Create leaf _BVHBuildNode_
      int firstPrimOffset = orderedPrims.size();
      for (int i = start; i < end; ++i) {
        int primNum = primitiveInfo[i].primitiveNumber;
        orderedPrims.push_back(primitives[primNum]);
      }
      node->InitLeaf(firstPrimOffset, nPrimitives, bounds);
      return node;
    } else {
      // Partition primitives based on _splitMethod_
      switch (splitMethod) {
        case SplitMethod::Middle: {
          // Partition primitives through node's midpoint
          Float pmid =
              (centroidBounds.pmin[dim] + centroidBounds.pmax[dim]) / 2;
          BVHPrimitiveInfo *midPtr = std::partition(
              &primitiveInfo[start], &primitiveInfo[end - 1] + 1,
              [dim, pmid](const BVHPrimitiveInfo &pi) {
                return pi.centroid[dim] < pmid;
              });
          mid = midPtr - &primitiveInfo[0];
          // For lots of prims with large overlapping bounding boxes, this
          // may fail to partition; in that case don't break and fall
          // through
          // to EqualCounts.
          if (mid != start && mid != end) break;
        }
        case SplitMethod::EqualCounts: {
          // Partition primitives into equally-sized subsets
          mid = (start + end) / 2;
          std::nth_element(&primitiveInfo[start], &primitiveInfo[mid],
                           &primitiveInfo[end - 1] + 1,
                           [dim](const BVHPrimitiveInfo &a,
                                 const BVHPrimitiveInfo &b) {
                             return a.centroid[dim] < b.centroid[dim];
                           });
          break;
        }
        case SplitMethod::SAH:
        default: {
          // Partition primitives using approximate SAH
          if (nPrimitives <= 2) {
            // Partition primitives into equally-sized subsets
            mid = (start + end) / 2;
            std::nth_element(&primitiveInfo[start], &primitiveInfo[mid],
                             &primitiveInfo[end - 1] + 1,
                             [dim](const BVHPrimitiveInfo &a,
                                   const BVHPrimitiveInfo &b) {
                               return a.centroid[dim] <
                                   b.centroid[dim];
                             });
          } else {
            // Allocate _BucketInfo_ for SAH partition buckets
            constexpr int nBuckets = 12;
            BucketInfo buckets[nBuckets];

            // Initialize _BucketInfo_ for SAH partition buckets
            for (int i = start; i < end; ++i) {
              int b = nBuckets *
                  centroidBounds.Offset(
                      primitiveInfo[i].centroid)[dim];
              if (b == nBuckets) b = nBuckets - 1;
              MIN_ASSERT(b >= 0);
              MIN_ASSERT(b < nBuckets);
              buckets[b].count++;
              buckets[b].bounds =
                  Union(buckets[b].bounds, primitiveInfo[i].bounds);
            }

            // Compute costs for splitting after each bucket
            Float cost[nBuckets - 1];
            for (int i = 0; i < nBuckets - 1; ++i) {
              Bounds3f b0, b1;
              int count0 = 0, count1 = 0;
              for (int j = 0; j <= i; ++j) {
                b0 = Union(b0, buckets[j].bounds);
                count0 += buckets[j].count;
              }
              for (int j = i + 1; j < nBuckets; ++j) {
                b1 = Union(b1, buckets[j].bounds);
                count1 += buckets[j].count;
              }
              cost[i] = 1 +
                  (count0 * b0.SurfaceArea() +
                      count1 * b1.SurfaceArea()) /
                      bounds.SurfaceArea();
            }

            // Find bucket to split at that minimizes SAH metric
            Float minCost = cost[0];
            int minCostSplitBucket = 0;
            for (int i = 1; i < nBuckets - 1; ++i) {
              if (cost[i] < minCost) {
                minCost = cost[i];
                minCostSplitBucket = i;
              }
            }

            // Either create leaf or split primitives at selected SAH
            // bucket
            Float leafCost = nPrimitives;
            if (nPrimitives > maxPrimsInNode || minCost < leafCost) {
              BVHPrimitiveInfo *pmid = std::partition(
                  &primitiveInfo[start], &primitiveInfo[end - 1] + 1,
                  [=](const BVHPrimitiveInfo &pi) {
                    int b = nBuckets *
                        centroidBounds.Offset(pi.centroid)[dim];
                    if (b == nBuckets) b = nBuckets - 1;
                    MIN_ASSERT(b >= 0);
                    MIN_ASSERT(b < nBuckets);
                    return b <= minCostSplitBucket;
                  });
              mid = pmid - &primitiveInfo[0];
            } else {
              // Create leaf _BVHBuildNode_
              int firstPrimOffset = orderedPrims.size();
              for (int i = start; i < end; ++i) {
                int primNum = primitiveInfo[i].primitiveNumber;
                orderedPrims.push_back(primitives[primNum]);
              }
              node->InitLeaf(firstPrimOffset, nPrimitives, bounds);
              return node;
            }
          }
          break;
        }
      }
      node->InitInterior(dim,
                         recursiveBuild(primitiveInfo, start, mid,
                                        totalNodes, orderedPrims),
                         recursiveBuild(primitiveInfo, mid, end,
                                        totalNodes, orderedPrims));
    }
  }
  return node;
}
BVHBuildNode *BVHAccel::HLBVHBuild(const std::vector<BVHPrimitiveInfo> &primitiveInfo,
                                   int *totalNodes,
                                   std::vector<std::shared_ptr<Shape>> &orderedPrims) const {
// Compute bounding box of all primitive centroids
  Bounds3f bounds;
  for (const BVHPrimitiveInfo &pi : primitiveInfo)
    bounds = Union(bounds, pi.centroid);

  // Compute Morton indices of primitives
  std::vector<MortonPrimitive> mortonPrims(primitiveInfo.size());
  for (uint32_t i = 0; i < primitiveInfo.size(); i++) {
    constexpr int mortonBits = 10;
    constexpr int mortonScale = 1 << mortonBits;
    mortonPrims[i].primitiveIndex = primitiveInfo[i].primitiveNumber;
    Vector3f centroidOffset = bounds.Offset(primitiveInfo[i].centroid);
    mortonPrims[i].mortonCode = EncodeMorton3(centroidOffset * Float(mortonScale));
  }

  // Radix sort primitive Morton indices
  RadixSort(&mortonPrims);

  // Create LBVH treelets at bottom of BVH

  // Find intervals of primitives for each treelet
  std::vector<LBVHTreelet> treeletsToBuild;
  for (int start = 0, end = 1; end <= (int)mortonPrims.size(); ++end) {
#ifdef HAVE_BINARY_CONSTANTS
    uint32_t mask = 0b00111111111111000000000000000000;
#else
    uint32_t mask = 0x3ffc0000;
#endif
    if (end == (int)mortonPrims.size() ||
        ((mortonPrims[start].mortonCode & mask) !=
            (mortonPrims[end].mortonCode & mask))) {
      // Add entry to _treeletsToBuild_ for this treelet
      int nPrimitives = end - start;
      int maxBVHNodes = 2 * nPrimitives;
      BVHBuildNode *nodes = new BVHBuildNode[maxBVHNodes];
      treeletsToBuild.push_back({start, nPrimitives, nodes});
      start = end;
    }
  }

  // Create LBVHs for treelets in parallel
  std::atomic<int> atomicTotal(0), orderedPrimsOffset(0);
  orderedPrims.resize(primitives.size());
  for (uint32_t i = 0; i < treeletsToBuild.size(); i++) {
    // Generate _i_th LBVH treelet
    int nodesCreated = 0;
    const int firstBitIndex = 29 - 12;
    LBVHTreelet &tr = treeletsToBuild[i];
    tr.buildNodes =
        emitLBVH(tr.buildNodes, primitiveInfo, &mortonPrims[tr.startIndex],
                 tr.nPrimitives, &nodesCreated, orderedPrims,
                 &orderedPrimsOffset, firstBitIndex);
    atomicTotal += nodesCreated;
  }
  *totalNodes = atomicTotal;

  // Create and return SAH BVH from LBVH treelets
  std::vector<BVHBuildNode *> finishedTreelets;
  finishedTreelets.reserve(treeletsToBuild.size());
  for (LBVHTreelet &treelet : treeletsToBuild)
    finishedTreelets.push_back(treelet.buildNodes);
  return buildUpperSAH(finishedTreelets, 0, finishedTreelets.size(),
                       totalNodes);
}
BVHBuildNode *BVHAccel::emitLBVH(BVHBuildNode *&buildNodes,
                                 const std::vector<BVHPrimitiveInfo> &primitiveInfo,
                                 MortonPrimitive *mortonPrims,
                                 int nPrimitives,
                                 int *totalNodes,
                                 std::vector<std::shared_ptr<Shape>> &orderedPrims,
                                 std::atomic<int> *orderedPrimsOffset,
                                 int bitIndex) const {
  MIN_ASSERT(nPrimitives > 0);
  if (bitIndex == -1 || nPrimitives < maxPrimsInNode) {
    // Create and return leaf node of LBVH treelet
    (*totalNodes)++;
    BVHBuildNode *node = buildNodes++;
    Bounds3f bounds;
    int firstPrimOffset = orderedPrimsOffset->fetch_add(nPrimitives);
    for (int i = 0; i < nPrimitives; ++i) {
      int primitiveIndex = mortonPrims[i].primitiveIndex;
      orderedPrims[firstPrimOffset + i] = primitives[primitiveIndex];
      bounds = Union(bounds, primitiveInfo[primitiveIndex].bounds);
    }
    node->InitLeaf(firstPrimOffset, nPrimitives, bounds);
    return node;
  } else {
    int mask = 1 << bitIndex;
    // Advance to next subtree level if there's no LBVH split for this bit
    if ((mortonPrims[0].mortonCode & mask) ==
        (mortonPrims[nPrimitives - 1].mortonCode & mask))
      return emitLBVH(buildNodes, primitiveInfo, mortonPrims, nPrimitives,
                      totalNodes, orderedPrims, orderedPrimsOffset,
                      bitIndex - 1);

    // Find LBVH split point for this dimension
    int searchStart = 0, searchEnd = nPrimitives - 1;
    while (searchStart + 1 != searchEnd) {
      MIN_ASSERT(searchStart != searchEnd);
      int mid = (searchStart + searchEnd) / 2;
      if ((mortonPrims[searchStart].mortonCode & mask) ==
          (mortonPrims[mid].mortonCode & mask))
        searchStart = mid;
      else {
        MIN_ASSERT(mortonPrims[mid].mortonCode & mask ==
                 mortonPrims[searchEnd].mortonCode & mask);
        searchEnd = mid;
      }
    }
    int splitOffset = searchEnd;
    MIN_ASSERT(splitOffset <= nPrimitives - 1);
    MIN_ASSERT(mortonPrims[splitOffset - 1].mortonCode & mask !=
             mortonPrims[splitOffset].mortonCode & mask);

    // Create and return interior LBVH node
    (*totalNodes)++;
    BVHBuildNode *node = buildNodes++;
    BVHBuildNode *lbvh[2] = {
        emitLBVH(buildNodes, primitiveInfo, mortonPrims, splitOffset,
                 totalNodes, orderedPrims, orderedPrimsOffset,
                 bitIndex - 1),
        emitLBVH(buildNodes, primitiveInfo, &mortonPrims[splitOffset],
                 nPrimitives - splitOffset, totalNodes, orderedPrims,
                 orderedPrimsOffset, bitIndex - 1)};
    int axis = bitIndex % 3;
    node->InitInterior(axis, lbvh[0], lbvh[1]);
    return node;
  }
}
BVHBuildNode *BVHAccel::buildUpperSAH(std::vector<BVHBuildNode *> &treeletRoots,
                                      int start,
                                      int end,
                                      int *totalNodes) const {
  MIN_ASSERT(start < end);
  int nNodes = end - start;
  if (nNodes == 1) return treeletRoots[start];
  (*totalNodes)++;
  BVHBuildNode *node = new BVHBuildNode();

  // Compute bounds of all nodes under this HLBVH node
  Bounds3f bounds;
  for (int i = start; i < end; ++i)
    bounds = Union(bounds, treeletRoots[i]->bounds);

  // Compute bound of HLBVH node centroids, choose split dimension _dim_
  Bounds3f centroidBounds;
  for (int i = start; i < end; ++i) {
    Point3f centroid =
        (treeletRoots[i]->bounds.pmin + treeletRoots[i]->bounds.pmax) *
            0.5f;
    centroidBounds = Union(centroidBounds, centroid);
  }
  int dim = centroidBounds.MaximumExtent();
  // FIXME: if this hits, what do we need to do?
  // Make sure the SAH split below does something... ?
  MIN_ASSERT(centroidBounds.pmax[dim] != centroidBounds.pmin[dim]);

  // Allocate _BucketInfo_ for SAH partition buckets
  constexpr int nBuckets = 12;
  struct BucketInfo {
    int count = 0;
    Bounds3f bounds;
  };
  BucketInfo buckets[nBuckets];

  // Initialize _BucketInfo_ for HLBVH SAH partition buckets
  for (int i = start; i < end; ++i) {
    Float centroid = (treeletRoots[i]->bounds.pmin[dim] +
        treeletRoots[i]->bounds.pmax[dim]) *
        0.5f;
    int b =
        nBuckets * ((centroid - centroidBounds.pmin[dim]) /
            (centroidBounds.pmax[dim] - centroidBounds.pmin[dim]));
    if (b == nBuckets) b = nBuckets - 1;
    MIN_ASSERT(b >= 0);
    MIN_ASSERT(b < nBuckets);
    buckets[b].count++;
    buckets[b].bounds = Union(buckets[b].bounds, treeletRoots[i]->bounds);
  }

  // Compute costs for splitting after each bucket
  Float cost[nBuckets - 1];
  for (int i = 0; i < nBuckets - 1; ++i) {
    Bounds3f b0, b1;
    int count0 = 0, count1 = 0;
    for (int j = 0; j <= i; ++j) {
      b0 = Union(b0, buckets[j].bounds);
      count0 += buckets[j].count;
    }
    for (int j = i + 1; j < nBuckets; ++j) {
      b1 = Union(b1, buckets[j].bounds);
      count1 += buckets[j].count;
    }
    cost[i] = .125f +
        (count0 * b0.SurfaceArea() + count1 * b1.SurfaceArea()) /
            bounds.SurfaceArea();
  }

  // Find bucket to split at that minimizes SAH metric
  Float minCost = cost[0];
  int minCostSplitBucket = 0;
  for (int i = 1; i < nBuckets - 1; ++i) {
    if (cost[i] < minCost) {
      minCost = cost[i];
      minCostSplitBucket = i;
    }
  }

  // Split nodes and create interior HLBVH SAH node
  BVHBuildNode **pmid = std::partition(
      &treeletRoots[start], &treeletRoots[end - 1] + 1,
      [=](const BVHBuildNode *node) {
        Float centroid =
            (node->bounds.pmin[dim] + node->bounds.pmax[dim]) * 0.5f;
        int b = nBuckets *
            ((centroid - centroidBounds.pmin[dim]) /
                (centroidBounds.pmax[dim] - centroidBounds.pmin[dim]));
        if (b == nBuckets) b = nBuckets - 1;
        MIN_ASSERT(b >= 0);
        MIN_ASSERT(b < nBuckets);
        return b <= minCostSplitBucket;
      });
  int mid = pmid - &treeletRoots[0];
  MIN_ASSERT(mid > start);
  MIN_ASSERT(mid < end);
  node->InitInterior(
      dim, this->buildUpperSAH(treeletRoots, start, mid, totalNodes),
      this->buildUpperSAH(treeletRoots, mid, end, totalNodes));
  return node;
}
int BVHAccel::flattenBVHTree(BVHBuildNode *node, int *offset) {
  LinearBVHNode *linearNode = &nodes[*offset];
  linearNode->bounds = node->bounds;
  int myOffset = (*offset)++;
  if (node->nPrimitives > 0) {
    MIN_ASSERT(!node->children[0] && !node->children[1]);
    MIN_ASSERT(node->nPrimitives < 65536);
    linearNode->primitivesOffset = node->firstPrimOffset;
    linearNode->nPrimitives = node->nPrimitives;
  } else {
    // Create interior flattened BVH node
    linearNode->axis = node->splitAxis;
    linearNode->nPrimitives = 0;
    flattenBVHTree(node->children[0], offset);
    linearNode->secondChildOffset =
        flattenBVHTree(node->children[1], offset);
  }
  return myOffset;
}


BVHAccel::~BVHAccel() {
  free(nodes);
}

bool BVHAccel::Intersect(const Ray &ray, SurfaceIntersection &isect) const {
  if (!nodes) return false;
  bool hit = false;
  Vector3f invDir(1 / ray.d.x, 1 / ray.d.y, 1 / ray.d.z);
  int dirIsNeg[3] = {invDir.x < 0, invDir.y < 0, invDir.z < 0};
  // Follow ray through BVH nodes to find primitive intersections
  int toVisitOffset = 0, currentNodeIndex = 0;
  int nodesToVisit[64];
  while (true) {
    const LinearBVHNode *node = &nodes[currentNodeIndex];
    // Check ray against BVH node
    if (node->bounds.IntersectP(ray, invDir, dirIsNeg)) {
      if (node->nPrimitives > 0) {
        // Intersect ray with primitives in leaf BVH node
        for (int i = 0; i < node->nPrimitives; ++i)
          if (primitives[node->primitivesOffset + i]->Intersect(
              ray, isect))
            hit = true;
        if (toVisitOffset == 0) break;
        currentNodeIndex = nodesToVisit[--toVisitOffset];
      } else {
        // Put far BVH node on _nodesToVisit_ stack, advance to near
        // node
        if (dirIsNeg[node->axis]) {
          nodesToVisit[toVisitOffset++] = currentNodeIndex + 1;
          currentNodeIndex = node->secondChildOffset;
        } else {
          nodesToVisit[toVisitOffset++] = node->secondChildOffset;
          currentNodeIndex = currentNodeIndex + 1;
        }
      }
    } else {
      if (toVisitOffset == 0) break;
      currentNodeIndex = nodesToVisit[--toVisitOffset];
    }
  }
  return hit;
}

bool BVHAccel::IntersectP(const Ray &ray) const {
  if (!nodes) return false;
  Vector3f invDir(1 / ray.d.x, 1 / ray.d.y, 1 / ray.d.z);
  int dirIsNeg[3] = {invDir.x < 0, invDir.y < 0, invDir.z < 0};
  // Follow ray through BVH nodes to find primitive intersections
  int toVisitOffset = 0, currentNodeIndex = 0;
  int nodesToVisit[64];
  while (true) {
    const LinearBVHNode *node = &nodes[currentNodeIndex];
    // Check ray against BVH node
    if (node->bounds.IntersectP(ray, invDir, dirIsNeg)) {
      if (node->nPrimitives > 0) {
        // Intersect ray with primitives in leaf BVH node
        for (int i = 0; i < node->nPrimitives; ++i)
          if (primitives[node->primitivesOffset + i]->IntersectP(
              ray)) return true;
        if (toVisitOffset == 0) break;
        currentNodeIndex = nodesToVisit[--toVisitOffset];
      } else {
        // Put far BVH node on _nodesToVisit_ stack, advance to near
        // node
        if (dirIsNeg[node->axis]) {
          nodesToVisit[toVisitOffset++] = currentNodeIndex + 1;
          currentNodeIndex = node->secondChildOffset;
        } else {
          nodesToVisit[toVisitOffset++] = node->secondChildOffset;
          currentNodeIndex = currentNodeIndex + 1;
        }
      }
    } else {
      if (toVisitOffset == 0) break;
      currentNodeIndex = nodesToVisit[--toVisitOffset];
    }
  }
  return false;
}

void BVHAccel::AddShape(const std::vector<std::shared_ptr<Shape>> &shape) {
  primitives.insert(primitives.end(), shape.begin(), shape.end());
}

void BVHAccel::Build() {
  if (primitives.empty()) return;
  // Build BVH from _primitives_
  // Initialize _primitiveInfo_ array for primitives
  std::vector<BVHPrimitiveInfo> primitiveInfo(primitives.size());
  for (size_t i = 0; i < primitives.size(); ++i) {
    primitiveInfo[i] = {i, primitives[i]->WorldBound()};
  }

  // Build BVH tree for primitives using _primitiveInfo_
  int totalNodes = 0;
  std::vector<std::shared_ptr<Shape>> orderedPrims;
  orderedPrims.reserve(primitives.size());
  BVHBuildNode *root;
  if (splitMethod == SplitMethod::HLBVH)
    root = HLBVHBuild(primitiveInfo, &totalNodes, orderedPrims);
  else
    root = recursiveBuild(primitiveInfo, 0, primitives.size(),
                          &totalNodes, orderedPrims);
  primitives.swap(orderedPrims);
  primitiveInfo.resize(0);
  MIN_INFO("BVH created with {} nodes for {}", totalNodes, (int)primitives.size());

  nodes = new LinearBVHNode[totalNodes];
  int offset = 0;
  flattenBVHTree(root, &offset);
  MIN_ASSERT(totalNodes == offset);
}
void BVHAccel::initialize(const Json &json) {
  auto str = Value<std::string>(json, "split_method", "sah");
  if (str == "sah") {
    splitMethod = BVHAccel::SplitMethod::SAH;
  }
  maxPrimsInNode = Value(json, "maxnodeprims", 4);
}
MIN_IMPLEMENTATION(Accelerator, BVHAccel, "bvh")

}

