#pragma once

#include <min/common/util.h>
#include <min/common/interface.h>
#include <min/common/json.h>
#include <min/math/math.h>
#include <filesystem>

namespace min {

using Point3f = Vector3f;
using Point3 = Vector3;
using Point3d = Vector3d;
using Point3i = Vector3i;
using Point2i = Vector2i;
using Point2f = Vector2f;
using Point2 = Vector2;

using Normal3f = Vector3f;
using Normal3 = Vector3;
using Normal3d = Vector3d;
using Normal3i = Vector3i;
using Normal2i = Vector2i;
using Normal2f = Vector2f;

// Forward declaration
class Film;
class FilmTile;
class Intersection;
class SurfaceIntersection;
class Scene;
class Shape;
class VisibilityTester;
class BSDFSample;

namespace fs = std::filesystem;

static constexpr Float kShadowEpsilon = 0.0001f;

#ifndef HAVE_HEX_FP_CONSTANTS
static const double kDoubleOneMinusEpsilon = 0.99999999999999989;
static const float kFloatOneMinusEpsilon = 0.99999994;
#else
static const double kDoubleOneMinusEpsilon = 0x1.fffffffffffffp-1;
static const float kFloatOneMinusEpsilon = 0x1.fffffep-1;
#endif

#ifdef MIN_USE_DOUBLE
static const Float kOneMinusEpsilon = kDoubleOneMinusEpsilon;
#else
static const Float kOneMinusEpsilon = kFloatOneMinusEpsilon;
#endif

}


