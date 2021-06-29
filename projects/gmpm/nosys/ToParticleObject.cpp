#include <cstring>
#include <vector>
#include <zeno/ParticlesObject.h>
#include <zeno/zen.h>

#include "../ZensimGeometry.h"
#include "zensim/tpls/fmt/color.h"
#include "zensim/tpls/fmt/format.h"

namespace zen {

struct ToParticleObject : zen::INode {
  void apply() override {
    fmt::print(fg(fmt::color::green), "begin executing ToParticleObject\n");
    auto &zspars = get_input("ZSParticles")->as<ZenoParticles>()->get();
    auto pars = zen::IObject::make<ParticlesObject>();
    auto &pos = pars->pos;
    auto &vel = pars->vel;

    zs::match([&pos, &vel](auto &zspars) {
      const auto cnt = zspars.size();
      pos.resize(cnt);
      vel.resize(cnt);
      for (auto &&[dst, src] : zs::zip(pos, zspars.X))
        dst = glm::vec3{src[0], src[1], src[2]};
      for (auto &&[dst, src] : zs::zip(vel, zspars.V))
        dst = glm::vec3{src[0], src[1], src[2]};
    })(zspars);

    fmt::print(fg(fmt::color::cyan), "done executing ToParticleObject\n");
    set_output("pars", pars);
  }
};

static int defToParticleObject = zen::defNodeClass<ToParticleObject>(
    "ToParticleObject", {/* inputs: */ {"ZSParticles"},
                         /* outputs: */ {"pars"}, /* params: */ {},
                         /* category: */ {"ZensimGeometry"}});

} // namespace zen
