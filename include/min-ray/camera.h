#pragma once

#include <min-ray/object.h>

namespace min::ray {

/**
 * \brief Generic camera interface
 * 
 * This class provides an abstract interface to cameras in Nori and
 * exposes the ability to sample their response function. By default, only
 * a perspective camera implementation exists, but you may choose to
 * implement other types (e.g. an environment camera, or a physically-based 
 * camera model that simulates the behavior actual lenses)
 */
class Camera : public NoriObject {
 public:
  /**
     * \brief Importance sample a ray according to the camera's response function
     *
     * \param ray
     *    A ray data structure to be filled with a position 
     *    and direction value
     *
     * \param samplePosition
     *    Denotes the desired sample position on the film
     *    expressed in fractional pixel coordinates
     *
     * \param apertureSample
     *    A uniformly distributed 2D vector that is used to sample
     *    a position on the aperture of the sensor if necessary.
     *
     * \return
     *    An importance weight associated with the sampled ray.
     *    This accounts for the difference in the camera response
     *    function and the sampling density.
     */
  virtual Color3f SampleRay(Ray3f &ray,
                            const Point2f &samplePosition,
                            const Point2f &apertureSample) const = 0;

  /// Return the size of the output image in pixels
  const Vector2i &GetOutputSize() const { return output_size; }

  /// Return the camera's reconstruction filter in image space
  const ReconstructionFilter *GetReconstructionFilter() const { return rfilter; }

  /**
     * \brief Return the type of object (i.e. Mesh/Camera/etc.) 
     * provided by this instance
     * */
  EClassType getClassType() const { return ECamera; }

 protected:
  Vector2i output_size;
  ReconstructionFilter *rfilter;
};

}  // namespace min::ray
