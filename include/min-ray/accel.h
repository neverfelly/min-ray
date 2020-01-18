
#pragma once

#include <min-ray/mesh.h>

namespace min::ray {

/**
 * \brief Acceleration data structure for ray intersection queries
 *
 * The current implementation falls back to a brute force loop
 * through the geometry.
 */
class Accel {
 public:
  /**
     * \brief Register a triangle mesh for inclusion in the acceleration
     * data structure
     *
     * This function can only be used before \ref build() is called
     */
  void AddMesh(Mesh *mesh);

  /// Build the acceleration data structure (currently a no-op)
  void Build();

  /// Return an axis-aligned box that bounds the scene
  const BoundingBox3f &GetBoundingBox() const { return bbox; }

  /**
     * \brief Intersect a ray against all triangles stored in the scene and
     * return detailed intersection information
     *
     * \param ray
     *    A 3-dimensional ray data structure with minimum/maximum extent
     *    information
     *
     * \param its
     *    A detailed intersection record, which will be filled by the
     *    intersection query
     *
     * \param shadowRay
     *    \c true if this is a shadow ray query, i.e. a query that only aims to
     *    find out whether the ray is blocked or not without returning detailed
     *    intersection information.
     *
     * \return \c true if an intersection was found
     */
  bool Intersect(const Ray3f &ray, Intersection &its, bool shadowRay) const;

 private:
  Mesh *mesh = nullptr;  ///< Mesh (only a single one for now)
  BoundingBox3f bbox;    ///< Bounding box of the entire scene
};

}  // namespace min::ray
