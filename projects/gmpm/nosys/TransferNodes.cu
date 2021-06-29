#include "../ZensimContainer.h"
#include "../ZensimGeometry.h"
#include "../ZensimModel.h"
#include "../ZensimObject.h"
#include "zensim/container/HashTable.hpp"
#include "zensim/cuda/execution/ExecutionPolicy.cuh"
#include "zensim/simulation/transfer/G2P.hpp"
#include "zensim/cuda/simulation/transfer/P2G.hpp"
#include "zensim/tpls/fmt/color.h"
#include "zensim/tpls/fmt/format.h"
#include <zeno/NumericObject.h>

namespace zeno {

struct P2G : zeno::INode {
  void apply() override {
    fmt::print(fg(fmt::color::green), "begin executing P2G\n");
    auto &model = get_input("ZSModel")->as<ZenoConstitutiveModel>()->get();
    // auto &particles = get_input("ZSParticles")->as<ZenoParticles>()->get();
    ZenoParticleObjects parObjPtrs{};
    if (get_input("ZSParticles")->as<ZenoParticles>())
      parObjPtrs.push_back(get_input("ZSParticles")->as<ZenoParticles>());
    else if (get_input("ZSParticles")->as<ZenoParticleList>()) {
      auto &list = get_input("ZSParticles")->as<ZenoParticleList>()->get();
      parObjPtrs.insert(parObjPtrs.end(), list.begin(), list.end());
    }
    auto &partition = get_input("ZSPartition")->as<ZenoPartition>()->get();
    auto &grid = get_input("ZSGrid")->as<ZenoGrid>()->get();

    // auto stepDt = std::get<float>(get_param("dt"));
    auto stepDt = get_input("dt")->as<zeno::NumericObject>()->get<float>();

    auto cudaPol = zs::cuda_exec().device(0);

    for (auto &&parObjPtr : parObjPtrs) {
      auto &particles = parObjPtr->get();
      zs::match(
          [&](auto &constitutiveModel, auto &obj, auto &partition, auto &grid)
              -> std::enable_if_t<
                  zs::remove_cvref_t<decltype(obj)>::dim ==
                      zs::remove_cvref_t<decltype(partition)>::dim &&
                  zs::remove_cvref_t<decltype(obj)>::dim ==
                      zs::remove_cvref_t<decltype(grid)>::dim> {
            cudaPol({obj.size()},
                    zs::P2GTransfer{zs::wrapv<zs::execspace_e::cuda>{},
                                    zs::wrapv<zs::transfer_scheme_e::apic>{},
                                    stepDt, constitutiveModel, obj, partition,
                                    grid});
          })(model, particles, partition, grid);
    }
    fmt::print(fg(fmt::color::cyan), "done executing P2G\n");
  }
};

static int defP2G = zeno::defNodeClass<P2G>(
    "P2G",
    {/* inputs: */ {"dt", "ZSModel", "ZSParticles", "ZSGrid", "ZSPartition"},
     /* outputs: */ {},
     /* params: */ {/*{"float", "dt", "0.0001"}*/},
     /* category: */ {"simulation"}});

struct G2P : zeno::INode {
  void apply() override {
    fmt::print(fg(fmt::color::green), "begin executing G2P\n");
    auto &model = get_input("ZSModel")->as<ZenoConstitutiveModel>()->get();
    auto &grid = get_input("ZSGrid")->as<ZenoGrid>()->get();
    auto &partition = get_input("ZSPartition")->as<ZenoPartition>()->get();
    // auto &particles = get_input("ZSParticles")->as<ZenoParticles>()->get();
    ZenoParticleObjects parObjPtrs{};
    if (get_input("ZSParticles")->as<ZenoParticles>())
      parObjPtrs.push_back(get_input("ZSParticles")->as<ZenoParticles>());
    else if (get_input("ZSParticles")->as<ZenoParticleList>()) {
      auto &list = get_input("ZSParticles")->as<ZenoParticleList>()->get();
      parObjPtrs.insert(parObjPtrs.end(), list.begin(), list.end());
    }

    // auto stepDt = std::get<float>(get_param("dt"));
    auto stepDt = get_input("dt")->as<zeno::NumericObject>()->get<float>();

    auto cudaPol = zs::cuda_exec().device(0);
    for (auto &&parObjPtr : parObjPtrs) {
      auto &particles = parObjPtr->get();
      zs::match(
          [&](auto &constitutiveModel, auto &grid, auto &partition, auto &obj)
              -> std::enable_if_t<
                  zs::remove_cvref_t<decltype(obj)>::dim ==
                      zs::remove_cvref_t<decltype(partition)>::dim &&
                  zs::remove_cvref_t<decltype(obj)>::dim ==
                      zs::remove_cvref_t<decltype(grid)>::dim> {
            // fmt::print("{} particles g2p\n", obj.size());
            cudaPol({obj.size()},
                    zs::G2PTransfer{zs::wrapv<zs::execspace_e::cuda>{},
                                    zs::wrapv<zs::transfer_scheme_e::apic>{},
                                    stepDt, constitutiveModel, grid, partition,
                                    obj});
          })(model, grid, partition, particles);
    }
    fmt::print(fg(fmt::color::cyan), "done executing G2P\n");
  }
};

static int defG2P = zeno::defNodeClass<G2P>(
    "G2P",
    {/* inputs: */ {"dt", "ZSModel", "ZSParticles", "ZSGrid", "ZSPartition"},
     /* outputs: */ {},
     /* params: */ {/*{"float", "dt", "0.0001"}*/},
     /* category: */ {"simulation"}});

} // namespace zen