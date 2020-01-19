
#include <min-ray/rfilter.h>

namespace min::ray {

/**
 * Windowed Gaussian filter with configurable extent
 * and standard deviation. Often produces pleasing 
 * results, but may introduce too much blurring.
 */
class GaussianFilter : public ReconstructionFilter {
 public:
  GaussianFilter(const PropertyList &propList) {
    /* Half filter size */
    radius = propList.getFloat("radius", 2.0f);
    /* Standard deviation of the Gaussian */
    m_stddev = propList.getFloat("stddev", 0.5f);
  }

  float Evaluate(float x) const {
    float alpha = -1.0f / (2.0f * m_stddev * m_stddev);
    return std::max(0.0f,
                    std::exp(alpha * x * x) -
                        std::exp(alpha * radius * radius));
  }

  std::string ToString() const {
    return tfm::format("GaussianFilter[radius=%f, stddev=%f]", radius, m_stddev);
  }

 protected:
  float m_stddev;
};

/**
 * Separable reconstruction filter by Mitchell and Netravali
 * 
 * D. Mitchell, A. Netravali, Reconstruction filters for computer graphics, 
 * Proceedings of SIGGRAPH 88, Computer Graphics 22(4), pp. 221-228, 1988.
 */
class MitchellNetravaliFilter : public ReconstructionFilter {
 public:
  MitchellNetravaliFilter(const PropertyList &propList) {
    /* Filter size in pixels */
    radius = propList.getFloat("radius", 2.0f);
    /* B parameter from the paper */
    m_B = propList.getFloat("B", 1.0f / 3.0f);
    /* C parameter from the paper */
    m_C = propList.getFloat("C", 1.0f / 3.0f);
  }

  float Evaluate(float x) const {
    x = std::abs(2.0f * x / radius);
    float x2 = x * x, x3 = x2 * x;

    if (x < 1) {
      return 1.0f / 6.0f * ((12 - 9 * m_B - 6 * m_C) * x3 + (-18 + 12 * m_B + 6 * m_C) * x2 + (6 - 2 * m_B));
    } else if (x < 2) {
      return 1.0f / 6.0f * ((-m_B - 6 * m_C) * x3 + (6 * m_B + 30 * m_C) * x2 + (-12 * m_B - 48 * m_C) * x + (8 * m_B + 24 * m_C));
    } else {
      return 0.0f;
    }
  }

  std::string ToString() const {
    return tfm::format("MitchellNetravaliFilter[radius=%f, B=%f, C=%f]", radius, m_B, m_C);
  }

 protected:
  float m_B, m_C;
};

/// Tent filter
class TentFilter : public ReconstructionFilter {
 public:
  TentFilter(const PropertyList &) {
    radius = 1.0f;
  }

  float Evaluate(float x) const {
    return std::max(0.0f, 1.0f - std::abs(x));
  }

  std::string ToString() const {
    return "TentFilter[]";
  }
};

/// Box filter -- fastest, but prone to aliasing
class BoxFilter : public ReconstructionFilter {
 public:
  BoxFilter(const PropertyList &) {
    radius = 0.5f;
  }

  float Evaluate(float) const {
    return 1.0f;
  }

  std::string ToString() const {
    return "BoxFilter[]";
  }
};

NORI_REGISTER_CLASS(GaussianFilter, "gaussian");
NORI_REGISTER_CLASS(MitchellNetravaliFilter, "mitchell");
NORI_REGISTER_CLASS(TentFilter, "tent");
NORI_REGISTER_CLASS(BoxFilter, "box");

}  // namespace min::ray
