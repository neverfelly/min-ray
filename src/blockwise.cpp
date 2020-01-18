
#include <filesystem/resolver.h>
#include <min-ray/bitmap.h>
#include <min-ray/block.h>
#include <min-ray/camera.h>
#include <min-ray/gui.h>
#include <min-ray/integrator.h>
#include <min-ray/parser.h>
#include <min-ray/sampler.h>
#include <min-ray/scene.h>
#include <min-ray/timer.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <thread>

namespace min::ray {

/// Blockwise render mode
class Blockwise : public RenderMode {
 public:
  Blockwise(const PropertyList &) {}

  virtual ~Blockwise() {}

  void render(Scene *scene, const std::string &filename) {
    const Camera *camera = scene->getCamera();
    Vector2i outputSize = camera->getOutputSize();
    scene->getIntegrator()->preprocess(scene);

    /* Create a block generator (i.e. a work scheduler) */
    BlockGenerator blockGenerator(outputSize, NORI_BLOCK_SIZE);

    /* Allocate memory for the entire output image and clear it */
    ImageBlock result(outputSize, camera->getReconstructionFilter());
    result.clear();

    /* Create a window that visualizes the partially rendered result */
    nanogui::init();
    NoriScreen *screen = new NoriScreen(result);

    /* Do the following in parallel and asynchronously */
    std::thread render_thread([&] {
      cout << "Rendering .. ";
      cout.flush();
      Timer timer;

      tbb::blocked_range<int> range(0, blockGenerator.getBlockCount());

      auto map = [&](const tbb::blocked_range<int> &range) {
        /* Allocate memory for a small image block to be rendered
	               by the current thread */
        ImageBlock block(Vector2i(NORI_BLOCK_SIZE),
                         camera->getReconstructionFilter());

        /* Create a clone of the sampler for the current thread */
        std::unique_ptr<Sampler> sampler(scene->getSampler()->clone());

        for (int i = range.begin(); i < range.end(); ++i) {
          /* Request an image block from the block generator */
          blockGenerator.next(block);

          /* Inform the sampler about the block to be rendered */
          sampler->prepare(block);

          /* Render all contained pixels */
          renderBlock(scene, sampler.get(), block);

          /* The image block has been processed. Now add it to
	                   the "big" block that represents the entire image */
          result.put(block);
        }
      };

      /// Uncomment the following line for single threaded rendering
      // map(range);

      /// Default: parallel rendering
      tbb::parallel_for(range, map);

      cout << "done. (took " << timer.elapsedString() << ")" << endl;
    });

    /* Enter the application main loop */
    nanogui::mainloop();

    /* Shut down the user interface */
    render_thread.join();

    delete screen;
    nanogui::shutdown();

    /* Now turn the rendered image block into
	       a properly normalized bitmap */
    std::unique_ptr<Bitmap> bitmap(result.toBitmap());

    /* Determine the filename of the output bitmap */
    std::string outputName = filename;
    size_t lastdot = outputName.find_last_of(".");
    if (lastdot != std::string::npos)
      outputName.erase(lastdot, std::string::npos);
    outputName += ".exr";

    /* Save using the OpenEXR format */
    bitmap->save(outputName);
  }

  void renderBlock(const Scene *scene, Sampler *sampler, ImageBlock &block) {
    const Camera *camera = scene->getCamera();
    const Integrator *integrator = scene->getIntegrator();

    Point2i offset = block.getOffset();
    Vector2i size = block.getSize();

    /* Clear the block contents */
    block.clear();

    /* For each pixel and pixel sample sample */
    for (int y = 0; y < size.y(); ++y) {
      for (int x = 0; x < size.x(); ++x) {
        for (uint32_t i = 0; i < sampler->getSampleCount(); ++i) {
          Point2f pixelSample = Point2f((float)(x + offset.x()), (float)(y + offset.y())) + sampler->next2D();
          Point2f apertureSample = sampler->next2D();

          /* Sample a ray from the camera */
          Ray3f ray;
          Color3f value = camera->sampleRay(ray, pixelSample, apertureSample);

          /* Compute the incident radiance */
          value *= integrator->Li(scene, sampler, ray);

          /* Store in the image block */
          block.put(pixelSample, value);
        }
      }
    }
  }

  std::string toString() const {
    return tfm::format("Blockwise[]");
  }

 private:
};
NORI_REGISTER_CLASS(Blockwise, "blockwise");
}  // namespace min::ray