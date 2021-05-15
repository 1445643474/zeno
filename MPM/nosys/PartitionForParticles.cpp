#include "../ZensimContainer.h"
#include "../ZensimGeometry.h"
//#include "zensim/cuda/execution/ExecutionPolicy.cuh"
#include "zensim/geometry/VdbLevelSet.h"
#include "zensim/simulation/sparsity/SparsityCompute.hpp"

namespace zenbase {

struct SpatialPartitionForParticles : zen::INode {
  void apply() override {
    auto partition = zen::IObject::make<ZenoPartition>();

    auto dx = std::get<float>(get_param("dx"));
    auto blocklen = std::get<int>(get_param("block_side_length"));
    // pass in FloatGrid::Ptr
    auto &particles = get_input("ZSParticles")->as<ZenoParticles>()->get();

#if 1
    partition->get() = zs::partition_for_particles(particles, dx, blocklen);
#else
    auto tmp = zs::partition_for_particles(particles, dx, blocklen);
    auto a = zs::match([](auto &t) { return t.size(); })(tmp);
    auto b = zs::match([](auto &p) { return p.size(); })(particles);
    fmt::print("{} blocks for {} particles\n", a, b);
    partition->get() = std::move(tmp);
#endif

    set_output("ZSPartition", partition);
  }
};

static int defSpatialPartitionForParticles = zen::defNodeClass<
    SpatialPartitionForParticles>(
    "SpatialPartitionForParticles",
    {/* inputs: */ {"ZSParticles"},
     /* outputs: */ {"ZSPartition"},
     /* params: */ {{"float", "dx", "1"}, {"int", "block_side_length", "4"}},
     /* category: */ {"simulation"}});

} // namespace zenbase