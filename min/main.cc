#include <min/math/linalg.h>
#include <min/visual/film.h>
#include <min/visual/camera.h>
#include <min/visual/renderer.h>
#include <min/visual/scene.h>
#include <min/visual/aggregate.h>
#include <fstream>

using namespace min;

int main() {
  try {
    std::string tmp = "E:\\work\\min-ray\\assets\\mitsuba\\scene.json";
    fs::path path(tmp);
    GetFileResolver()->Prepend(path.parent_path());
    std::ifstream is(tmp);
    Json j;
    is >> j;
    auto camera = CreateInstance<Camera>(j["camera"]["type"], GetProps(j.at("camera")));
    auto scene = CreateInstance<Scene>("scene", "");
    auto accel = CreateInstance<Accelerator>(j["accelerator"]["type"], GetProps(j["accelerator"]));
    scene->SetCamera(camera);
    scene->SetAccelerator(accel);
    if (j.contains("lights")) {
      for (auto jlight : j["lights"]) {
        auto light = CreateInstance<Light>(jlight["type"], GetProps(jlight));
        scene->AddLight(light);
      }
    }
    for (auto jshape : j["shapes"]) {
      auto aggregate = CreateInstance<Aggregate>(jshape["type"], GetProps(jshape));
      scene->AddShape(aggregate->shapes);
      scene->AddLights(aggregate->lights);
    }
    scene->Build();
    auto renderer = CreateInstance<Renderer>(j["renderer"]["type"], GetProps(j.at("renderer")));
    renderer->SetScene(scene);
    renderer->Render();
  } catch (std::exception &e) {
    std::cout << e.what() << std::endl;
  }
  return 0;
}

