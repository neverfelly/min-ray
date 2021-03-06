#pragma once

#include "defs.h"

namespace min {

template<typename T>
using TPoint2 = TVector2<T>;
template<typename T>
using TPoint3 = TVector3<T>;
template<typename T>
using TPoint4 = TVector4<T>;

template<typename T>
using TNormal2 = TVector2<T>;
template<typename T>
using TNormal3 = TVector3<T>;
template<typename T>
using TNormal4 = TVector4<T>;

class Ray {
 public:
  Ray() : tmax(kInfinity), time(0.0f) {}
  Ray(const Point3 &o, const Vector3 &d, Float tmax = kInfinity, Float time = 0.0f)
      : o(o), d(d), tmax(tmax), time(time) {}
  Point3 operator()(Float t) { return o + t * d; }
  Point3 o;
  Vector3 d;
  mutable Float tmax;
  Float time;
};

inline Point3f OffsetRayOrigin(const Point3f &p, const Vector3f &pError,
                               const Normal3f &n, const Vector3f &w) {
  Float d = Dot(Abs(n), pError);
  Vector3f offset = d * Vector3f(n);
  if (Dot(w, n) < 0) offset = -offset;
  Point3f po = p + offset;
  // Round offset point _po_ away from _p_
  for (int i = 0; i < 3; ++i) {
    if (offset[i] > 0)
      po[i] = NextFloatUp(po[i]);
    else if (offset[i] < 0)
      po[i] = NextFloatDown(po[i]);
  }
  return po;
}

template<typename T>
class Bounds2 {
  template<typename V>
  using Point2 = TVector<V, 2>;
  template<typename V>
  using Vector2 = Point2<V>;
 public:
  Bounds2() {
    T min_num = std::numeric_limits<T>::lowest();
    T max_num = std::numeric_limits<T>::max();
    pmin = TVector<T, 2>(max_num, max_num);
    pmax = TVector<T, 2>(min_num, min_num);
  }

  explicit Bounds2(const Point2<T> &p) : pmin(p), pmax(p) {}
  Bounds2(const Point2<T> &p1, const Point2<T> &p2) {
    pmin = Point2<T>(std::min<T>(p1.x, p2.x), std::min<T>(p1.y, p2.y));
    pmax = Point2<T>(std::max<T>(p1.x, p2.x), std::max<T>(p1.y, p2.y));
  }
  template<typename U>
  explicit operator Bounds2<U>() const {
    return Bounds2<U>((Point2<U>) pmin, (Point2<U>) pmax);
  }

  Vector2<T> Diagonal() const { return pmax - pmin; }

  T Area() const {
    Vector2<T> d = pmax - pmin;
    return (d.x * d.y);
  }

  std::string ToString() const {
    return fmt::format("[min={}, pmax={}]", pmin.ToString(), pmax.ToString());
  }
  Point2<T> pmin, pmax;
};

template<typename T>
Bounds2<T> Intersect(const Bounds2<T> &b1, const Bounds2<T> &b2) {
  Bounds2<T> ret;
  ret.pmin = Max(b1.pmin, b2.pmin);
  ret.pmax = Min(b1.pmax, b2.pmax);
  return ret;
}

template<typename T>
class Bounds3 {
  template<typename V>
  using Point3 = TVector<V, 3>;
 public:
  // Bounds3 Public Methods
  Bounds3() {
    T min_num = std::numeric_limits<T>::lowest();
    T max_num = std::numeric_limits<T>::max();
    pmin = Point3<T>(max_num, max_num, max_num);
    pmax = Point3<T>(min_num, min_num, min_num);
  }
  explicit Bounds3(const Point3<T> &p) : pmin(p), pmax(p) {}
  Bounds3(const Point3<T> &p1, const Point3<T> &p2)
      : pmin(std::min(p1.x, p2.x), std::min(p1.y, p2.y),
             std::min(p1.z, p2.z)),
        pmax(std::max(p1.x, p2.x), std::max(p1.y, p2.y),
             std::max(p1.z, p2.z)) {}


  const Point3<T> &operator[](int i) const {
    MIN_ASSERT(i == 0 || i == 1);
    return i == 0 ? pmin : pmax;
  }

  Point3<T> &operator[](int i) {
    MIN_ASSERT(i == 0 || i == 1);
    return i == 0 ? pmin : pmax;
  }

  TVector3<T> Diagonal() const { return pmax - pmin; }

  T SurfaceArea() const {
    TVector3<T> d = Diagonal();
    return d.x * d.y * d.z;
  }

  int MaximumExtent() const {
    TVector3<T> d = Diagonal();
    if (d.x > d.y && d.x > d.z) return 0;
    else if (d.y > d.z) return 1;
    else return 2;
  }

  TVector3<T> Offset(const Point3<T> &p) const {
    TVector3<T> o = p - pmin;
    if (pmax.x > pmin.x) o.x /= pmax.x - pmin.x;
    if (pmax.y > pmin.y) o.y /= pmax.y - pmin.y;
    if (pmax.z > pmin.z) o.z /= pmax.z - pmin.z;
    return o;
  }

  inline bool IntersectP(const Ray &ray, const Vector3f &invDir,
                                     const int dirIsNeg[3]) const {
    const Bounds3f &bounds = *this;
    // Check for ray intersection against $x$ and $y$ slabs
    Float tMin = (bounds[dirIsNeg[0]].x - ray.o.x) * invDir.x;
    Float tMax = (bounds[1 - dirIsNeg[0]].x - ray.o.x) * invDir.x;
    Float tyMin = (bounds[dirIsNeg[1]].y - ray.o.y) * invDir.y;
    Float tyMax = (bounds[1 - dirIsNeg[1]].y - ray.o.y) * invDir.y;

    // Update _tMax_ and _tyMax_ to ensure robust bounds intersection
    tMax *= 1 + 2 * Gamma(3);
    tyMax *= 1 + 2 * Gamma(3);
    if (tMin > tyMax || tyMin > tMax) return false;
    if (tyMin > tMin) tMin = tyMin;
    if (tyMax < tMax) tMax = tyMax;

    // Check for ray intersection against $z$ slab
    Float tzMin = (bounds[dirIsNeg[2]].z - ray.o.z) * invDir.z;
    Float tzMax = (bounds[1 - dirIsNeg[2]].z - ray.o.z) * invDir.z;

    // Update _tzMax_ to ensure robust bounds intersection
    tzMax *= 1 + 2 * Gamma(3);
    if (tMin > tzMax || tzMin > tMax) return false;
    if (tzMin > tMin) tMin = tzMin;
    if (tzMax < tMax) tMax = tzMax;
    return (tMin < ray.tmax) && (tMax > 0);
  }

  Point3<T> pmin, pmax;
};

typedef Bounds2<Float> Bounds2f;
typedef Bounds2<int> Bounds2i;
typedef Bounds3<Float> Bounds3f;
typedef Bounds3<int> Bounds3i;

template <typename T>
bool InsideExclusive(const TPoint2<T> &p, const Bounds2<T> &b) {
  return p.x >= b.pmin.x && p.x < b.pmax.x && p.y >= b.pmin.y && p.y < b.pmax.y;
}

template <typename T>
bool InsideExclusive(const TPoint3<T> &p, const Bounds3<T> &b) {
  return p.x >= b.pmin.x && p.x < b.pmax.x && p.y >= b.pmin.y && p.y < b.pmax.y && p.z >= b.pmin.z && p.z < b.pmax.z;
}

class Bounds2iIterator : public std::forward_iterator_tag {
 private:
  Point2i p;
  const Bounds2i *bounds;
  void Advance() {
    ++p.x;
    if (p.x == bounds->pmax.x) {
      p.x = bounds->pmin.x;
      ++p.y;
    }
  }
 public:
  Bounds2iIterator(const Bounds2i &b, const Point2i &pt) : p(pt), bounds(&b) {}

  Bounds2iIterator operator++() {
    Advance();
    return *this;
  }

  Bounds2iIterator operator++(int) {
    Bounds2iIterator old = *this;
    Advance();
    return old;
  }

  bool operator==(const Bounds2iIterator &b) const {
    return p == b.p && bounds == b.bounds;
  }

  bool operator!=(const Bounds2iIterator &b) const {
    return p != b.p || bounds != b.bounds;
  }

  Point2i operator*() const { return p; }
};

inline Bounds2iIterator begin(const Bounds2i &b) {
  return Bounds2iIterator(b, b.pmin);
}

inline Bounds2iIterator end(const Bounds2i &b) {
  Point2i pend(b.pmin.x, b.pmax.y);
  if (b.pmin.x >= b.pmax.x || b.pmin.y >= b.pmax.y) pend = b.pmin;
  return Bounds2iIterator(b, pend);
}

template <typename T>
bool Inside(const TPoint3<T> &p, const Bounds3<T> &b) {
  return p.x >= b.pmin.x && p.x <= b.pmax.x && p.y >= b.pmin.y && p.y <= b.pmax.y && p.z >= b.pmin.z && p.z <= b.pmax.z;
}

template <typename T>
Bounds3<T> Union(const Bounds3<T> &b, const TPoint3<T> &p) {
  Bounds3<T> ret;
  ret.pmin = Min(b.pmin, p);
  ret.pmax = Max(b.pmax, p);
  return ret;
}

template <typename T>
Bounds3<T> Union(const Bounds3<T> &b, const Bounds3<T> &b2) {
  Bounds3<T> ret;
  ret.pmin = Min(b.pmin, b2.pmin);
  ret.pmax = Max(b.pmax, b2.pmax);
  return ret;
}

}


