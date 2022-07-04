#include "../Structures.hpp"
#include "../Utils.hpp"
// #include "Ccd.hpp"
#include "Ccds.hpp"
#include "zensim/Logger.hpp"
#include "zensim/cuda/execution/ExecutionPolicy.cuh"
#include "zensim/execution/ExecutionPolicy.hpp"
#include "zensim/geometry/Distance.hpp"
#include "zensim/geometry/PoissonDisk.hpp"
#include "zensim/geometry/SpatialQuery.hpp"
#include "zensim/geometry/VdbLevelSet.h"
#include "zensim/geometry/VdbSampler.h"
#include "zensim/io/MeshIO.hpp"
#include "zensim/math/bit/Bits.h"
#include "zensim/physics/ConstitutiveModel.hpp"
#include "zensim/types/Property.h"
#include <atomic>
#include <thrust/device_vector.h>
#include <thrust/reduce.h>
#include <zeno/VDBGrid.h>
#include <zeno/types/ListObject.h>
#include <zeno/types/NumericObject.h>
#include <zeno/types/PrimitiveObject.h>
#include <zeno/types/StringObject.h>

namespace zeno {

template <typename TileVecT, int codim = 3>
zs::Vector<zs::AABBBox<3, typename TileVecT::value_type>>
retrieve_bounding_volumes(zs::CudaExecutionPolicy &pol, const TileVecT &vtemp,
                          const zs::SmallString &xTag,
                          const typename ZenoParticles::particles_t &eles,
                          zs::wrapv<codim>, int voffset) {
  using namespace zs;
  using T = typename TileVecT::value_type;
  using bv_t = AABBBox<3, T>;
  static_assert(codim >= 1 && codim <= 4, "invalid co-dimension!\n");
  constexpr auto space = execspace_e::cuda;
  zs::Vector<bv_t> ret{eles.get_allocator(), eles.size()};
  pol(range(eles.size()), [eles = proxy<space>({}, eles),
                           bvs = proxy<space>(ret),
                           vtemp = proxy<space>({}, vtemp),
                           codim_v = wrapv<codim>{}, xTag,
                           voffset] ZS_LAMBDA(int ei) mutable {
    constexpr int dim = RM_CVREF_T(codim_v)::value;
    auto inds =
        eles.template pack<dim>("inds", ei).template reinterpret_bits<int>() +
        voffset;
    auto x0 = vtemp.template pack<3>(xTag, inds[0]);
    bv_t bv{x0, x0};
    for (int d = 1; d != dim; ++d)
      merge(bv, vtemp.template pack<3>(xTag, inds[d]));
    bvs[ei] = bv;
  });
  return ret;
}
template <typename TileVecT0, typename TileVecT1, int codim = 3>
zs::Vector<zs::AABBBox<3, typename TileVecT0::value_type>>
retrieve_bounding_volumes(zs::CudaExecutionPolicy &pol, const TileVecT0 &verts,
                          const zs::SmallString &xTag,
                          const typename ZenoParticles::particles_t &eles,
                          zs::wrapv<codim>, const TileVecT1 &vtemp,
                          const zs::SmallString &dirTag, float stepSize,
                          int voffset) {
  using namespace zs;
  using T = typename TileVecT0::value_type;
  using bv_t = AABBBox<3, T>;
  static_assert(codim >= 1 && codim <= 4, "invalid co-dimension!\n");
  constexpr auto space = execspace_e::cuda;
  Vector<bv_t> ret{eles.get_allocator(), eles.size()};
  pol(zs::range(eles.size()), [eles = proxy<space>({}, eles),
                               bvs = proxy<space>(ret),
                               verts = proxy<space>({}, verts),
                               vtemp = proxy<space>({}, vtemp),
                               codim_v = wrapv<codim>{}, xTag, dirTag, stepSize,
                               voffset] ZS_LAMBDA(int ei) mutable {
    constexpr int dim = RM_CVREF_T(codim_v)::value;
    auto inds =
        eles.template pack<dim>("inds", ei).template reinterpret_bits<int>() +
        voffset;
    auto x0 = verts.template pack<3>(xTag, inds[0]);
    auto dir0 = vtemp.template pack<3>(dirTag, inds[0]);
    bv_t bv{get_bounding_box(x0, x0 + stepSize * dir0)};
    for (int d = 1; d != dim; ++d) {
      auto x = verts.template pack<3>(xTag, inds[d]);
      auto dir = vtemp.template pack<3>(dirTag, inds[d]);
      merge(bv, x);
      merge(bv, x + stepSize * dir);
    }
    bvs[ei] = bv;
  });
  return ret;
}

struct CodimStepping : INode {
  using T = double;
  using Ti = zs::conditional_t<zs::is_same_v<T, double>, zs::i64, zs::i32>;
  using dtiles_t = zs::TileVector<T, 32>;
  using tiles_t = typename ZenoParticles::particles_t;
  using vec3 = zs::vec<T, 3>;
  using ivec3 = zs::vec<int, 3>;
  using ivec2 = zs::vec<int, 2>;
  using mat2 = zs::vec<T, 2, 2>;
  using mat3 = zs::vec<T, 3, 3>;
  using pair_t = zs::vec<int, 2>;
  using pair3_t = zs::vec<int, 3>;
  using bvh_t = zs::LBvh<3, 32, int, T>;
  using bv_t = zs::AABBBox<3, T>;

  static constexpr vec3 s_groundNormal{0, 1, 0};
  inline static const char s_meanMassTag[] = "MeanMass";
  inline static const char s_meanSurfEdgeLengthTag[] = "MeanSurfEdgeLength";
  inline static bool projectDBC = true;
  inline static bool BCsatisfied = false;
  inline static T updateZoneTol = 1e-1;
  inline static T consTol = 1e-2;
  inline static T armijoParam = 1e-4;
  inline static bool useGD = false;
  inline static T boxDiagSize2 = 0;
  inline static T avgNodeMass = 0;
  inline static T targetGRes = 1e-2;
#define s_enableAdaptiveSetting 0
// static constexpr bool s_enableAdaptiveSetting = false;
#define s_enableContact 1
// static constexpr bool s_enableContact = true;
#define s_enableDCDCheck 0
  // static constexpr bool s_enableDCDCheck = false;

  inline static std::size_t estNumCps = 1000000;
  inline static T augLagCoeff = 1e4;
  inline static T cgRel = 1e-2;
  inline static T pnRel = 1e-2;
  inline static T kappaMax = 1e8;
  inline static T kappaMin = 1e4;
  inline static T kappa0 = 1e4;
  inline static T kappa = kappa0;
  inline static T xi = 0; // 1e-2; // 2e-3;
  inline static T dHat = 0.0025;
  inline static vec3 extForce;

  template <typename T> static inline T computeHb(const T d2, const T dHat2) {
#if 0
    T hess = 0;
    if (d2 < dHat2) {
      T t2 = d2 - dHat2;
      hess = (std::log(d2 / dHat2) * (T)-2.0 - t2 * (T)4.0 / d2) / (dHat2 * dHat2)
                + 1.0 / (d2 * d2) * (t2 / dHat2) * (t2 / dHat2);
    }
    return hess;
#else
    if (d2 >= dHat2)
      return 0;
    T t2 = d2 - dHat2;
    return ((std::log(d2 / dHat2) * -2 - t2 * 4 / d2) + (t2 / d2) * (t2 / d2));
#endif
  }

  template <
      typename VecT, int N = VecT::template range_t<0>::value,
      zs::enable_if_all<N % 3 == 0, N == VecT::template range_t<1>::value> = 0>
  static constexpr void rotate_hessian(zs::VecInterface<VecT> &H,
                                       const mat3 BCbasis[N / 3],
                                       const int BCorder[N / 3],
                                       const int BCfixed[], bool projectDBC) {
    // hessian rotation: trans^T hess * trans
    // left trans^T: multiplied on rows
    // right trans: multiplied on cols
    constexpr int NV = N / 3;
    // rotate and project
    for (int vi = 0; vi != NV; ++vi) {
      int offsetI = vi * 3;
      for (int vj = 0; vj != NV; ++vj) {
        int offsetJ = vj * 3;
        mat3 tmp{};
        for (int i = 0; i != 3; ++i)
          for (int j = 0; j != 3; ++j)
            tmp(i, j) = H(offsetI + i, offsetJ + j);
        // rotate
        tmp = BCbasis[vi].transpose() * tmp * BCbasis[vj];
        // project
        if (projectDBC || (!projectDBC && (BCfixed[vi] > 0 || BCfixed[vj] > 0)))
          if (BCorder[vi] > 0 || BCorder[vj] > 0) {
            if (vi == vj) {
              for (int i = 0; i != BCorder[vi]; ++i)
                for (int j = 0; j != BCorder[vj]; ++j)
                  tmp(i, j) = (i == j ? 1 : 0);
            } else {
              for (int i = 0; i != BCorder[vi]; ++i)
                for (int j = 0; j != BCorder[vj]; ++j)
                  tmp(i, j) = 0;
            }
          }
        for (int i = 0; i != 3; ++i)
          for (int j = 0; j != 3; ++j)
            H(offsetI + i, offsetJ + j) = tmp(i, j);
      }
    }
    return;
  }

  /// ref: codim-ipc
  struct IPCSystem {
    struct PrimitiveHandle {
      PrimitiveHandle(ZenoParticles &zsprim, std::size_t &vOffset,
                      std::size_t &sfOffset, std::size_t &seOffset,
                      std::size_t &svOffset, zs::wrapv<3>)
          : zsprim{zsprim}, verts{zsprim.getParticles<true>()},
            eles{zsprim.getQuadraturePoints()},
            etemp{zsprim.getQuadraturePoints().get_allocator(),
                  {{"He", 9 * 9}},
                  zsprim.numElements()},
            surfTris{zsprim.getQuadraturePoints()},
            surfEdges{zsprim[ZenoParticles::s_surfEdgeTag]},
            surfVerts{zsprim[ZenoParticles::s_surfVertTag]}, vOffset{vOffset},
            sfOffset{sfOffset}, seOffset{seOffset}, svOffset{svOffset},
            category{zsprim.category} {
        if (category != ZenoParticles::surface)
          throw std::runtime_error("dimension of 3 but is not surface");
        vOffset += verts.size();
        sfOffset += surfTris.size();
        seOffset += surfEdges.size();
        svOffset += surfVerts.size();
      }
      PrimitiveHandle(ZenoParticles &zsprim, std::size_t &vOffset,
                      std::size_t &sfOffset, std::size_t &seOffset,
                      std::size_t &svOffset, zs::wrapv<4>)
          : zsprim{zsprim}, verts{zsprim.getParticles<true>()},
            eles{zsprim.getQuadraturePoints()},
            etemp{zsprim.getQuadraturePoints().get_allocator(),
                  {{"He", 12 * 12}},
                  zsprim.numElements()},
            surfTris{zsprim[ZenoParticles::s_surfTriTag]},
            surfEdges{zsprim[ZenoParticles::s_surfEdgeTag]},
            surfVerts{zsprim[ZenoParticles::s_surfVertTag]}, vOffset{vOffset},
            sfOffset{sfOffset}, seOffset{seOffset}, svOffset{svOffset},
            category{zsprim.category} {
        if (category != ZenoParticles::tet)
          throw std::runtime_error("dimension of 4 but is not tetrahedra");
        vOffset += verts.size();
        sfOffset += surfTris.size();
        seOffset += surfEdges.size();
        svOffset += surfVerts.size();
      }

      T averageSurfEdgeLength(zs::CudaExecutionPolicy &pol) const {
        using namespace zs;
        constexpr auto space = execspace_e::cuda;
        if (zsprim.hasMeta(s_meanSurfEdgeLengthTag))
          return zsprim.readMeta(s_meanSurfEdgeLengthTag, zs::wrapt<T>{});
        auto &edges = surfEdges;
        Vector<T> edgeLengths{edges.get_allocator(), edges.size()};
        pol(Collapse{edges.size()},
            [edges = proxy<space>({}, edges), verts = proxy<space>({}, verts),
             edgeLengths =
                 proxy<space>(edgeLengths)] __device__(int ei) mutable {
              auto inds = edges.template pack<2>("inds", ei)
                              .template reinterpret_bits<int>();
              edgeLengths[ei] =
                  (verts.pack<3>("x0", inds[0]) - verts.pack<3>("x0", inds[1]))
                      .norm();
            });
        auto tmp = reduce(pol, edgeLengths) / edges.size();
        zsprim.setMeta(s_meanSurfEdgeLengthTag, tmp);
        return tmp;
      }

      decltype(auto) getVerts() const { return verts; }
      decltype(auto) getEles() const { return eles; }
      decltype(auto) getSurfTris() const { return surfTris; }
      decltype(auto) getSurfEdges() const { return surfEdges; }
      decltype(auto) getSurfVerts() const { return surfVerts; }

      ZenoParticles &zsprim;
      typename ZenoParticles::dtiles_t &verts;
      typename ZenoParticles::particles_t &eles;
      typename ZenoParticles::dtiles_t etemp;
      typename ZenoParticles::particles_t &surfTris;
      typename ZenoParticles::particles_t &surfEdges;
      // not required for codim obj
      typename ZenoParticles::particles_t &surfVerts;
      const std::size_t vOffset, sfOffset, seOffset, svOffset;
      ZenoParticles::category_e category;
    };

    T averageSurfEdgeLength(zs::CudaExecutionPolicy &pol) {
      T sumSurfEdgeLengths = 0;
      std::size_t sumSE = 0;
      for (auto &&primHandle : prims) {
        auto numSE = primHandle.getSurfEdges().size();
        sumSE += numSE;
        sumSurfEdgeLengths += primHandle.averageSurfEdgeLength(pol) * numSE;
      }
      return sumSurfEdgeLengths / sumSE;
    }

    ///
    auto getCnts() const {
      return zs::make_tuple(nPP.getVal(), nPE.getVal(), nPT.getVal(),
                            nEE.getVal(), ncsPT.getVal(), ncsEE.getVal());
    }
    void computeConstraints(zs::CudaExecutionPolicy &pol,
                            const zs::SmallString &tag) {
      using namespace zs;
      constexpr auto space = execspace_e::cuda;
      pol(Collapse{numDofs},
          [vtemp = proxy<space>({}, vtemp), tag] __device__(int vi) mutable {
            auto BCbasis = vtemp.pack<3, 3>("BCbasis", vi);
            auto BCtarget = vtemp.pack<3>("BCtarget", vi);
            int BCorder = vtemp("BCorder", vi);
            auto x = BCbasis.transpose() * vtemp.pack<3>(tag, vi);
            int d = 0;
            for (; d != BCorder; ++d)
              vtemp("cons", d, vi) = x[d] - BCtarget[d];
            for (; d != 3; ++d)
              vtemp("cons", d, vi) = 0;
          });
    }
    bool areConstraintsSatisfied(zs::CudaExecutionPolicy &pol) {
      using namespace zs;
      constexpr auto space = execspace_e::cuda;
      computeConstraints(pol, "xn");
      // auto res = infNorm(pol, vtemp, "cons");
      auto res = constraintResidual(pol);
      return res < 1e-2;
    }
    T checkDBCStatus(zs::CudaExecutionPolicy &pol) {
      using namespace zs;
      constexpr auto space = execspace_e::cuda;
      pol(Collapse{numDofs},
          [vtemp = proxy<space>({}, vtemp)] __device__(int vi) mutable {
            int BCorder = vtemp("BCorder", vi);
            if (BCorder > 0) {
              auto BCbasis = vtemp.pack<3, 3>("BCbasis", vi);
              auto BCtarget = vtemp.pack<3>("BCtarget", vi);
              auto cons = vtemp.pack<3>("cons", vi);
              auto xt = vtemp.pack<3>("xt", vi);
              auto x = vtemp.pack<3>("xn", vi);
              printf("%d-th vert (order [%d]): cur (%f, %f, %f) xt (%f, %f, %f)"
                     "\n\ttar(%f, %f, %f) cons (%f, %f, %f)\n",
                     vi, BCorder, (float)x[0], (float)x[1], (float)x[2],
                     (float)xt[0], (float)xt[1], (float)xt[2],
                     (float)BCtarget[0], (float)BCtarget[1], (float)BCtarget[2],
                     (float)cons[0], (float)cons[1], (float)cons[2]);
            }
          });
    }
    T constraintResidual(zs::CudaExecutionPolicy &pol,
                         bool maintainFixed = false) {
      using namespace zs;
      if (projectDBC)
        return 0;
      Vector<T> num{vtemp.get_allocator(), numDofs},
          den{vtemp.get_allocator(), numDofs};
      constexpr auto space = execspace_e::cuda;
      pol(Collapse{numDofs},
          [vtemp = proxy<space>({}, vtemp), den = proxy<space>(den),
           num = proxy<space>(num), maintainFixed] __device__(int vi) mutable {
            auto BCbasis = vtemp.pack<3, 3>("BCbasis", vi);
            auto BCtarget = vtemp.pack<3>("BCtarget", vi);
            int BCorder = vtemp("BCorder", vi);
            auto cons = vtemp.pack<3>("cons", vi);
            auto xt = vtemp.pack<3>("xt", vi);
            T n = 0, d_ = 0;
            // https://ipc-sim.github.io/file/IPC-supplement-A-technical.pdf Eq5
            for (int d = 0; d != BCorder; ++d) {
              n += zs::sqr(cons[d]);
              d_ += zs::sqr(col(BCbasis, d).dot(xt) - BCtarget[d]);
            }
            num[vi] = n;
            den[vi] = d_;
            if (maintainFixed) {
              if (d_ != 0) {
                if (zs::sqrt(n / d_) < 1e-6)
                  vtemp("BCfixed", vi) = 1;
              } else {
                if (zs::sqrt(n) < 1e-6)
                  vtemp("BCfixed", vi) = 1;
              }
            }
          });
      auto nsqr = reduce(pol, num);
      auto dsqr = reduce(pol, den);
      T ret = 0;
      if (dsqr == 0)
        ret = std::sqrt(nsqr);
      else
        ret = std::sqrt(nsqr / dsqr);
      return ret < 1e-6 ? 0 : ret;
    }
    void updateWholeBoundingBoxSize(zs::CudaExecutionPolicy &pol) const {
      using namespace zs;
      constexpr auto space = execspace_e::cuda;
      Vector<bv_t> box{vtemp.get_allocator(), 1};
      pol(Collapse{1},
          [bvh = proxy<space>(stBvh), box = proxy<space>(box)] __device__(
              int vi) mutable { box[0] = bvh.getNodeBV(0); });
      bv_t bv = box.getVal();
      boxDiagSize2 = (bv._max - bv._min).l2NormSqr();
    }
    void findCollisionConstraints(zs::CudaExecutionPolicy &pol, T dHat,
                                  T xi = 0) {
      nPP.setVal(0);
      nPE.setVal(0);
      nPT.setVal(0);
      nEE.setVal(0);

      ncsPT.setVal(0);
      ncsEE.setVal(0);
      {
        auto triBvs = retrieve_bounding_volumes(pol, vtemp, "xn", stInds,
                                                zs::wrapv<3>{}, 0);
        stBvh.refit(pol, triBvs);
        auto edgeBvs = retrieve_bounding_volumes(pol, vtemp, "xn", seInds,
                                                 zs::wrapv<2>{}, 0);
        seBvh.refit(pol, edgeBvs);
      }
      findCollisionConstraintsImpl(pol, dHat, xi, false);

      {
        auto triBvs = retrieve_bounding_volumes(pol, vtemp, "xn", coEles,
                                                zs::wrapv<3>{}, coOffset);
        bouStBvh.refit(pol, triBvs);
        auto edgeBvs = retrieve_bounding_volumes(pol, vtemp, "xn", coEdges,
                                                 zs::wrapv<2>{}, coOffset);
        bouSeBvh.refit(pol, edgeBvs);
      }
      findCollisionConstraintsImpl(pol, dHat, xi, true);
    }
    void findCollisionConstraintsImpl(zs::CudaExecutionPolicy &pol, T dHat,
                                      T xi, bool withBoundary = false) {
      using namespace zs;
      constexpr auto space = execspace_e::cuda;
      const auto dHat2 = dHat * dHat;

      /// pt
      pol(Collapse{svInds.size()},
          [svInds = proxy<space>({}, svInds),
           eles = proxy<space>({}, withBoundary ? coEles : stInds),
           vtemp = proxy<space>({}, vtemp),
           bvh = proxy<space>(withBoundary ? bouStBvh : stBvh),
           PP = proxy<space>(PP), nPP = proxy<space>(nPP),
           PE = proxy<space>(PE), nPE = proxy<space>(nPE),
           PT = proxy<space>(PT), nPT = proxy<space>(nPT),
           csPT = proxy<space>(csPT), ncsPT = proxy<space>(ncsPT), dHat, xi,
           thickness = xi + dHat,
           voffset = withBoundary ? coOffset : 0] __device__(int vi) mutable {
            vi = reinterpret_bits<int>(svInds("inds", vi));
            const auto dHat2 = zs::sqr(dHat + xi);
            int BCorder0 = vtemp("BCorder", vi);
            auto p = vtemp.template pack<3>("xn", vi);
            auto bv = bv_t{get_bounding_box(p - thickness, p + thickness)};
            bvh.iter_neighbors(bv, [&](int stI) {
              auto tri = eles.template pack<3>("inds", stI)
                             .template reinterpret_bits<int>() +
                         voffset;
              if (vi == tri[0] || vi == tri[1] || vi == tri[2])
                return;
              // all affected by sticky boundary conditions
              if (BCorder0 == 3 && vtemp("BCorder", tri[0]) == 3 &&
                  vtemp("BCorder", tri[1]) == 3 &&
                  vtemp("BCorder", tri[2]) == 3)
                return;
              // ccd
              auto t0 = vtemp.template pack<3>("xn", tri[0]);
              auto t1 = vtemp.template pack<3>("xn", tri[1]);
              auto t2 = vtemp.template pack<3>("xn", tri[2]);

              switch (pt_distance_type(p, t0, t1, t2)) {
              case 0: {
                if (dist2_pp(p, t0) < dHat2) {
                  auto no = atomic_add(exec_cuda, &nPP[0], 1);
                  PP[no] = pair_t{vi, tri[0]};
                  csPT[atomic_add(exec_cuda, &ncsPT[0], 1)] =
                      pair4_t{vi, tri[0], tri[1], tri[2]};
                }
                break;
              }
              case 1: {
                if (dist2_pp(p, t1) < dHat2) {
                  auto no = atomic_add(exec_cuda, &nPP[0], 1);
                  PP[no] = pair_t{vi, tri[1]};
                  csPT[atomic_add(exec_cuda, &ncsPT[0], 1)] =
                      pair4_t{vi, tri[0], tri[1], tri[2]};
                }
                break;
              }
              case 2: {
                if (dist2_pp(p, t2) < dHat2) {
                  auto no = atomic_add(exec_cuda, &nPP[0], 1);
                  PP[no] = pair_t{vi, tri[2]};
                  csPT[atomic_add(exec_cuda, &ncsPT[0], 1)] =
                      pair4_t{vi, tri[0], tri[1], tri[2]};
                }
                break;
              }
              case 3: {
                if (dist2_pe(p, t0, t1) < dHat2) {
                  auto no = atomic_add(exec_cuda, &nPE[0], 1);
                  PE[no] = pair3_t{vi, tri[0], tri[1]};
                  csPT[atomic_add(exec_cuda, &ncsPT[0], 1)] =
                      pair4_t{vi, tri[0], tri[1], tri[2]};
                }
                break;
              }
              case 4: {
                if (dist2_pe(p, t1, t2) < dHat2) {
                  auto no = atomic_add(exec_cuda, &nPE[0], 1);
                  PE[no] = pair3_t{vi, tri[1], tri[2]};
                  csPT[atomic_add(exec_cuda, &ncsPT[0], 1)] =
                      pair4_t{vi, tri[0], tri[1], tri[2]};
                }
                break;
              }
              case 5: {
                if (dist2_pe(p, t2, t0) < dHat2) {
                  auto no = atomic_add(exec_cuda, &nPE[0], 1);
                  PE[no] = pair3_t{vi, tri[2], tri[0]};
                  csPT[atomic_add(exec_cuda, &ncsPT[0], 1)] =
                      pair4_t{vi, tri[0], tri[1], tri[2]};
                }
                break;
              }
              case 6: {
                if (dist2_pt(p, t0, t1, t2) < dHat2) {
                  auto no = atomic_add(exec_cuda, &nPT[0], 1);
                  PT[no] = pair4_t{vi, tri[0], tri[1], tri[2]};
                  csPT[atomic_add(exec_cuda, &ncsPT[0], 1)] =
                      pair4_t{vi, tri[0], tri[1], tri[2]};
                }
                break;
              }
              default:
                break;
              }
            });
          });
      /// ee
      pol(Collapse{seInds.size()}, [seInds = proxy<space>({}, seInds),
                                    sedges = proxy<space>(
                                        {}, withBoundary ? coEdges : seInds),
                                    vtemp = proxy<space>({}, vtemp),
                                    bvh = proxy<space>(withBoundary ? bouSeBvh
                                                                    : seBvh),
                                    PP = proxy<space>(PP),
                                    nPP = proxy<space>(nPP),
                                    PE = proxy<space>(PE),
                                    nPE = proxy<space>(nPE),
                                    EE = proxy<space>(EE),
                                    nEE = proxy<space>(nEE),
                                    csEE = proxy<space>(csEE),
                                    ncsEE = proxy<space>(ncsEE), dHat, xi,
                                    thickness = xi + dHat,
                                    voffset =
                                        withBoundary
                                            ? coOffset
                                            : 0] __device__(int sei) mutable {
        const auto dHat2 = zs::sqr(dHat + xi);
        auto eiInds = seInds.template pack<2>("inds", sei)
                          .template reinterpret_bits<int>();
        bool selfFixed = vtemp("BCorder", eiInds[0]) == 3 &&
                         vtemp("BCorder", eiInds[1]) == 3;
        auto v0 = vtemp.template pack<3>("xn", eiInds[0]);
        auto v1 = vtemp.template pack<3>("xn", eiInds[1]);
        auto rv0 = vtemp.template pack<3>("x0", eiInds[0]);
        auto rv1 = vtemp.template pack<3>("x0", eiInds[1]);
        auto [mi, ma] = get_bounding_box(v0, v1);
        auto bv = bv_t{mi - thickness, ma + thickness};
        bvh.iter_neighbors(bv, [&](int sej) {
          if (voffset == 0 && sei < sej)
            return;
          auto ejInds = sedges.template pack<2>("inds", sej)
                            .template reinterpret_bits<int>() +
                        voffset;
          if (eiInds[0] == ejInds[0] || eiInds[0] == ejInds[1] ||
              eiInds[1] == ejInds[0] || eiInds[1] == ejInds[1])
            return;
          // all affected by sticky boundary conditions
          if (selfFixed && vtemp("BCorder", ejInds[0]) == 3 &&
              vtemp("BCorder", ejInds[1]) == 3)
            return;
          // ccd
          auto v2 = vtemp.template pack<3>("xn", ejInds[0]);
          auto v3 = vtemp.template pack<3>("xn", ejInds[1]);
          auto rv2 = vtemp.template pack<3>("x0", ejInds[0]);
          auto rv3 = vtemp.template pack<3>("x0", ejInds[1]);

          switch (ee_distance_type(v0, v1, v2, v3)) {
          case 0: {
            if (dist2_pp(v0, v2) < dHat2) {
              {
                auto no = atomic_add(exec_cuda, &nPP[0], 1);
                PP[no] = pair_t{eiInds[0], ejInds[0]};
                csEE[atomic_add(exec_cuda, &ncsEE[0], 1)] =
                    pair4_t{eiInds[0], eiInds[1], ejInds[0], ejInds[1]};
              }
            }
            break;
          }
          case 1: {
            if (dist2_pp(v0, v3) < dHat2) {
              {
                auto no = atomic_add(exec_cuda, &nPP[0], 1);
                PP[no] = pair_t{eiInds[0], ejInds[1]};
                csEE[atomic_add(exec_cuda, &ncsEE[0], 1)] =
                    pair4_t{eiInds[0], eiInds[1], ejInds[0], ejInds[1]};
              }
            }
            break;
          }
          case 2: {
            if (dist2_pe(v0, v2, v3) < dHat2) {
              {
                auto no = atomic_add(exec_cuda, &nPE[0], 1);
                PE[no] = pair3_t{eiInds[0], ejInds[0], ejInds[1]};
                csEE[atomic_add(exec_cuda, &ncsEE[0], 1)] =
                    pair4_t{eiInds[0], eiInds[1], ejInds[0], ejInds[1]};
              }
            }
            break;
          }
          case 3: {
            if (dist2_pp(v1, v2) < dHat2) {
              {
                auto no = atomic_add(exec_cuda, &nPP[0], 1);
                PP[no] = pair_t{eiInds[1], ejInds[0]};
                csEE[atomic_add(exec_cuda, &ncsEE[0], 1)] =
                    pair4_t{eiInds[0], eiInds[1], ejInds[0], ejInds[1]};
              }
            }
            break;
          }
          case 4: {
            if (dist2_pp(v1, v3) < dHat2) {
              {
                auto no = atomic_add(exec_cuda, &nPP[0], 1);
                PP[no] = pair_t{eiInds[1], ejInds[1]};
                csEE[atomic_add(exec_cuda, &ncsEE[0], 1)] =
                    pair4_t{eiInds[0], eiInds[1], ejInds[0], ejInds[1]};
              }
            }
            break;
          }
          case 5: {
            if (dist2_pe(v1, v2, v3) < dHat2) {
              {
                auto no = atomic_add(exec_cuda, &nPE[0], 1);
                PE[no] = pair3_t{eiInds[1], ejInds[0], ejInds[1]};
                csEE[atomic_add(exec_cuda, &ncsEE[0], 1)] =
                    pair4_t{eiInds[0], eiInds[1], ejInds[0], ejInds[1]};
              }
            }
            break;
          }
          case 6: {
            if (dist2_pe(v2, v0, v1) < dHat2) {
              {
                auto no = atomic_add(exec_cuda, &nPE[0], 1);
                PE[no] = pair3_t{ejInds[0], eiInds[0], eiInds[1]};
                csEE[atomic_add(exec_cuda, &ncsEE[0], 1)] =
                    pair4_t{eiInds[0], eiInds[1], ejInds[0], ejInds[1]};
              }
            }
            break;
          }
          case 7: {
            if (dist2_pe(v3, v0, v1) < dHat2) {
              {
                auto no = atomic_add(exec_cuda, &nPE[0], 1);
                PE[no] = pair3_t{ejInds[1], eiInds[0], eiInds[1]};
                csEE[atomic_add(exec_cuda, &ncsEE[0], 1)] =
                    pair4_t{eiInds[0], eiInds[1], ejInds[0], ejInds[1]};
              }
            }
            break;
          }
          case 8: {
            if (dist2_ee(v0, v1, v2, v3) < dHat2) {
              {
                auto no = atomic_add(exec_cuda, &nEE[0], 1);
                EE[no] = pair4_t{eiInds[0], eiInds[1], ejInds[0], ejInds[1]};
                csEE[atomic_add(exec_cuda, &ncsEE[0], 1)] =
                    pair4_t{eiInds[0], eiInds[1], ejInds[0], ejInds[1]};
              }
            }
            break;
          }
          default:
            break;
          }
        });
      });
    }
    bool checkSelfIntersection(zs::CudaExecutionPolicy &pol) {
      using namespace zs;
      constexpr auto space = execspace_e::cuda;
      const auto dHat2 = dHat * dHat;
      zs::Vector<int> intersected{vtemp.get_allocator(), 1};
      intersected.setVal(0);
      // self
      {
        auto edgeBvs = retrieve_bounding_volumes(pol, vtemp, "xn", seInds,
                                                 zs::wrapv<2>{}, 0);
        bvh_t seBvh;
        seBvh.build(pol, edgeBvs);
        pol(Collapse{stInds.size()},
            [stInds = proxy<space>({}, stInds),
             seInds = proxy<space>({}, seInds), vtemp = proxy<space>({}, vtemp),
             intersected = proxy<space>(intersected),
             bvh = proxy<space>(seBvh)] __device__(int sti) mutable {
              auto tri = stInds.template pack<3>("inds", sti)
                             .template reinterpret_bits<int>();
              auto t0 = vtemp.pack<3>("xn", tri[0]);
              auto t1 = vtemp.pack<3>("xn", tri[1]);
              auto t2 = vtemp.pack<3>("xn", tri[2]);
              auto bv = bv_t{get_bounding_box(t0, t1)};
              merge(bv, t2);
              bool allFixed = vtemp("BCorder", tri[0]) == 3 &&
                              vtemp("BCorder", tri[1]) == 3 &&
                              vtemp("BCorder", tri[2]) == 3;
              bvh.iter_neighbors(bv, [&](int sei) {
                auto line = seInds.template pack<2>("inds", sei)
                                .template reinterpret_bits<int>();
                if (tri[0] == line[0] || tri[0] == line[1] ||
                    tri[1] == line[0] || tri[1] == line[1] ||
                    tri[2] == line[0] || tri[2] == line[1])
                  return;
                // ignore intersection under sticky boundary conditions
                if (allFixed && vtemp("BCorder", line[0]) == 3 &&
                    vtemp("BCorder", line[1]) == 3)
                  return;
                // ccd
                if (et_intersected(vtemp.pack<3>("xn", line[0]),
                                   vtemp.pack<3>("xn", line[1]), t0, t1, t2))
                  if (intersected[0] == 0)
                    intersected[0] = 1;
                // atomic_cas(exec_cuda, &intersected[0], 0, 1);
              });
            });
      }
      // boundary
      {
        auto edgeBvs = retrieve_bounding_volumes(pol, vtemp, "xn", coEdges,
                                                 zs::wrapv<2>{}, coOffset);
        bvh_t seBvh;
        seBvh.build(pol, edgeBvs);
        pol(Collapse{stInds.size()},
            [stInds = proxy<space>({}, stInds),
             coEdges = proxy<space>({}, coEdges),
             vtemp = proxy<space>({}, vtemp),
             intersected = proxy<space>(intersected), bvh = proxy<space>(seBvh),
             coOffset = coOffset] __device__(int sti) mutable {
              auto tri = stInds.template pack<3>("inds", sti)
                             .template reinterpret_bits<int>();
              auto t0 = vtemp.pack<3>("xn", tri[0]);
              auto t1 = vtemp.pack<3>("xn", tri[1]);
              auto t2 = vtemp.pack<3>("xn", tri[2]);
              auto bv = bv_t{get_bounding_box(t0, t1)};
              merge(bv, t2);
              bool allFixed = vtemp("BCorder", tri[0]) == 3 &&
                              vtemp("BCorder", tri[1]) == 3 &&
                              vtemp("BCorder", tri[2]) == 3;
              bvh.iter_neighbors(bv, [&](int sei) {
                auto line = coEdges.template pack<2>("inds", sei)
                                .template reinterpret_bits<int>() +
                            coOffset;
                // ignore intersection under sticky boundary conditions
                if (allFixed && vtemp("BCorder", line[0]) == 3 &&
                    vtemp("BCorder", line[1]) == 3)
                  return;
                // ccd
                if (et_intersected(vtemp.pack<3>("xn", line[0]),
                                   vtemp.pack<3>("xn", line[1]), t0, t1, t2))
                  if (intersected[0] == 0)
                    intersected[0] = 1;
                // atomic_cas(exec_cuda, &intersected[0], 0, 1);
              });
            });
      }
      return intersected.getVal();
    }
    void findCCDConstraints(zs::CudaExecutionPolicy &pol, T alpha, T xi = 0) {
      ncsPT.setVal(0);
      ncsEE.setVal(0);
      {
        auto triBvs = retrieve_bounding_volumes(
            pol, vtemp, "xn", stInds, zs::wrapv<3>{}, vtemp, "dir", alpha, 0);
        stBvh.refit(pol, triBvs);
        auto edgeBvs = retrieve_bounding_volumes(
            pol, vtemp, "xn", seInds, zs::wrapv<2>{}, vtemp, "dir", alpha, 0);
        seBvh.refit(pol, edgeBvs);
      }
      findCCDConstraintsImpl(pol, alpha, xi, false);

      {
        auto triBvs =
            retrieve_bounding_volumes(pol, vtemp, "xn", coEles, zs::wrapv<3>{},
                                      vtemp, "dir", alpha, coOffset);
        bouStBvh.refit(pol, triBvs);
        auto edgeBvs =
            retrieve_bounding_volumes(pol, vtemp, "xn", coEdges, zs::wrapv<2>{},
                                      vtemp, "dir", alpha, coOffset);
        bouSeBvh.refit(pol, edgeBvs);
      }
      findCCDConstraintsImpl(pol, alpha, xi, true);
    }
    void findCCDConstraintsImpl(zs::CudaExecutionPolicy &pol, T alpha, T xi,
                                bool withBoundary = false) {
      using namespace zs;
      constexpr auto space = execspace_e::cuda;
      const auto dHat2 = dHat * dHat;

      /// pt
      pol(Collapse{svInds.size()},
          [svInds = proxy<space>({}, svInds),
           eles = proxy<space>({}, withBoundary ? coEles : stInds),
           vtemp = proxy<space>({}, vtemp),
           bvh = proxy<space>(withBoundary ? bouStBvh : stBvh),
           PP = proxy<space>(PP), nPP = proxy<space>(nPP),
           PE = proxy<space>(PE), nPE = proxy<space>(nPE),
           PT = proxy<space>(PT), nPT = proxy<space>(nPT),
           csPT = proxy<space>(csPT), ncsPT = proxy<space>(ncsPT), xi, alpha,
           voffset = withBoundary ? coOffset : 0] __device__(int vi) mutable {
            vi = reinterpret_bits<int>(svInds("inds", vi));
            auto p = vtemp.template pack<3>("xn", vi);
            auto dir = vtemp.template pack<3>("dir", vi);
            auto bv = bv_t{get_bounding_box(p, p + alpha * dir)};
            bv._min -= xi;
            bv._max += xi;
            bvh.iter_neighbors(bv, [&](int stI) {
              auto tri = eles.template pack<3>("inds", stI)
                             .template reinterpret_bits<int>() +
                         voffset;
              if (vi == tri[0] || vi == tri[1] || vi == tri[2])
                return;
              // all affected by sticky boundary conditions
              if (vtemp("BCorder", vi) == 3 && vtemp("BCorder", tri[0]) == 3 &&
                  vtemp("BCorder", tri[1]) == 3 &&
                  vtemp("BCorder", tri[2]) == 3)
                return;
              csPT[atomic_add(exec_cuda, &ncsPT[0], 1)] =
                  pair4_t{vi, tri[0], tri[1], tri[2]};
            });
          });
      /// ee
      pol(Collapse{seInds.size()},
          [seInds = proxy<space>({}, seInds),
           sedges = proxy<space>({}, withBoundary ? coEdges : seInds),
           vtemp = proxy<space>({}, vtemp),
           bvh = proxy<space>(withBoundary ? bouSeBvh : seBvh),
           PP = proxy<space>(PP), nPP = proxy<space>(nPP),
           PE = proxy<space>(PE), nPE = proxy<space>(nPE),
           EE = proxy<space>(PT), nEE = proxy<space>(nPT),
           csEE = proxy<space>(csEE), ncsEE = proxy<space>(ncsEE), xi, alpha,
           voffset = withBoundary ? coOffset : 0] __device__(int sei) mutable {
            auto eiInds = seInds.template pack<2>("inds", sei)
                              .template reinterpret_bits<int>();
            bool selfFixed = vtemp("BCorder", eiInds[0]) == 3 &&
                             vtemp("BCorder", eiInds[1]) == 3;
            auto v0 = vtemp.template pack<3>("xn", eiInds[0]);
            auto v1 = vtemp.template pack<3>("xn", eiInds[1]);
            auto dir0 = vtemp.template pack<3>("dir", eiInds[0]);
            auto dir1 = vtemp.template pack<3>("dir", eiInds[1]);
            auto bv = bv_t{get_bounding_box(v0, v0 + alpha * dir0)};
            merge(bv, v1);
            merge(bv, v1 + alpha * dir1);
            bv._min -= xi;
            bv._max += xi;
            bvh.iter_neighbors(bv, [&](int sej) {
              if (voffset == 0 && sei < sej)
                return;
              auto ejInds = sedges.template pack<2>("inds", sej)
                                .template reinterpret_bits<int>() +
                            voffset;
              if (eiInds[0] == ejInds[0] || eiInds[0] == ejInds[1] ||
                  eiInds[1] == ejInds[0] || eiInds[1] == ejInds[1])
                return;
              // all affected by sticky boundary conditions
              if (selfFixed && vtemp("BCorder", ejInds[0]) == 3 &&
                  vtemp("BCorder", ejInds[1]) == 3)
                return;
              csEE[atomic_add(exec_cuda, &ncsEE[0], 1)] =
                  pair4_t{eiInds[0], eiInds[1], ejInds[0], ejInds[1]};
            });
          });
    }
    ///
    void computeBarrierGradientAndHessian(zs::CudaExecutionPolicy &pol,
                                          const zs::SmallString &gTag = "grad");

    void intersectionFreeStepsize(zs::CudaExecutionPolicy &pol, T xi,
                                  T &stepSize) {
      using namespace zs;
      constexpr auto space = execspace_e::cuda;

      Vector<T> alpha{vtemp.get_allocator(), 1};
      alpha.setVal(stepSize);
      auto npt = ncsPT.getVal();
      pol(range(npt),
          [csPT = proxy<space>(csPT), vtemp = proxy<space>({}, vtemp),
           alpha = proxy<space>(alpha), stepSize, xi,
           coOffset = (int)coOffset] __device__(int pti) {
            auto ids = csPT[pti];
            auto p = vtemp.template pack<3>("xn", ids[0]);
            auto t0 = vtemp.template pack<3>("xn", ids[1]);
            auto t1 = vtemp.template pack<3>("xn", ids[2]);
            auto t2 = vtemp.template pack<3>("xn", ids[3]);
            auto dp = vtemp.template pack<3>("dir", ids[0]);
            auto dt0 = vtemp.template pack<3>("dir", ids[1]);
            auto dt1 = vtemp.template pack<3>("dir", ids[2]);
            auto dt2 = vtemp.template pack<3>("dir", ids[3]);
            T tmp = stepSize;
#if 1
            if (accd::ptccd(p, t0, t1, t2, dp, dt0, dt1, dt2, (T)0.1, xi, tmp))
#elif 1
            if (ticcd::ptccd(p, t0, t1, t2, dp, dt0, dt1, dt2, (T)0.1, xi, tmp))
#else
            if (pt_ccd(p, t0, t1, t2, dp, dt0, dt1, dt2, xi, tmp))
#endif
              atomic_min(exec_cuda, &alpha[0], tmp);
          });
      auto nee = ncsEE.getVal();
      pol(range(nee),
          [csEE = proxy<space>(csEE), vtemp = proxy<space>({}, vtemp),
           alpha = proxy<space>(alpha), stepSize, xi,
           coOffset = (int)coOffset] __device__(int eei) {
            auto ids = csEE[eei];
            auto ea0 = vtemp.template pack<3>("xn", ids[0]);
            auto ea1 = vtemp.template pack<3>("xn", ids[1]);
            auto eb0 = vtemp.template pack<3>("xn", ids[2]);
            auto eb1 = vtemp.template pack<3>("xn", ids[3]);
            auto dea0 = vtemp.template pack<3>("dir", ids[0]);
            auto dea1 = vtemp.template pack<3>("dir", ids[1]);
            auto deb0 = vtemp.template pack<3>("dir", ids[2]);
            auto deb1 = vtemp.template pack<3>("dir", ids[3]);
            auto tmp = stepSize;
#if 1
            if (accd::eeccd(ea0, ea1, eb0, eb1, dea0, dea1, deb0, deb1, (T)0.1,
                            xi, tmp))
#elif 1
            if (ticcd::eeccd(ea0, ea1, eb0, eb1, dea0, dea1, deb0, deb1, (T)0.1, xi, tmp))
#else
            if (ee_ccd(ea0, ea1, eb0, eb1, dea0, dea1, deb0, deb1, xi, tmp))
#endif
              atomic_min(exec_cuda, &alpha[0], tmp);
          });
      stepSize = alpha.getVal();
    }
    void groundIntersectionFreeStepsize(zs::CudaExecutionPolicy &pol,
                                        T &stepSize) {
      using namespace zs;
      constexpr T slackness = 0.8;
      constexpr auto space = execspace_e::cuda;

      zs::Vector<T> finalAlpha{coVerts.get_allocator(), 1};
      finalAlpha.setVal(stepSize);
      pol(Collapse{coOffset},
          [vtemp = proxy<space>({}, vtemp),
           // boundary
           gn = s_groundNormal, finalAlpha = proxy<space>(finalAlpha),
           stepSize] ZS_LAMBDA(int vi) mutable {
            // this vert affected by sticky boundary conditions
            if (vtemp("BCorder", vi) == 3)
              return;
            auto dir = vtemp.pack<3>("dir", vi);
            auto coef = gn.dot(dir);
            if (coef < 0) { // impacting direction
              auto x = vtemp.pack<3>("xn", vi);
              auto dist = gn.dot(x);
              auto maxAlpha = (dist * 0.8) / (-coef);
              if (maxAlpha < stepSize)
                atomic_min(exec_cuda, &finalAlpha[0], maxAlpha);
            }
          });
      stepSize = finalAlpha.getVal();
      fmt::print(fg(fmt::color::dark_cyan), "ground alpha: {}\n", stepSize);
    }
    ///
    void computeBoundaryBarrierGradientAndHessian(
        zs::CudaExecutionPolicy &pol, const zs::SmallString &gTag = "grad") {
      using namespace zs;
      constexpr auto space = execspace_e::cuda;
      pol(range(coOffset),
          [vtemp = proxy<space>({}, vtemp), tempPB = proxy<space>({}, tempPB),
           gTag, gn = s_groundNormal, dHat2 = dHat * dHat, kappa = kappa,
           projectDBC = projectDBC] ZS_LAMBDA(int vi) mutable {
            auto x = vtemp.pack<3>("xn", vi);
            auto dist = gn.dot(x);
            auto dist2 = dist * dist;
            auto t = dist2 - dHat2;
            auto g_b = t * zs::log(dist2 / dHat2) * -2 - (t * t) / dist2;
            auto H_b = (zs::log(dist2 / dHat2) * -2.0 - t * 4.0 / dist2) +
                       1.0 / (dist2 * dist2) * (t * t);
            if (dist2 < dHat2) {
              auto grad = -gn * (kappa * g_b * 2 * dist);
              for (int d = 0; d != 3; ++d)
                atomic_add(exec_cuda, &vtemp(gTag, d, vi), grad(d));
            }

            auto param = 4 * H_b * dist2 + 2 * g_b;
            auto hess = mat3::zeros();
            if (dist2 < dHat2 && param > 0) {
              auto nn = dyadic_prod(gn, gn);
              hess = (kappa * param) * nn;
            }

            // make_pd(hess);
            mat3 BCbasis[1] = {vtemp.pack<3, 3>("BCbasis", vi)};
            int BCorder[1] = {(int)vtemp("BCorder", vi)};
            int BCfixed[1] = {(int)vtemp("BCfixed", vi)};
            rotate_hessian(hess, BCbasis, BCorder, BCfixed, projectDBC);
            tempPB.tuple<9>("H", vi) = hess;
            for (int i = 0; i != 3; ++i)
              for (int j = 0; j != 3; ++j) {
                atomic_add(exec_cuda, &vtemp("P", i * 3 + j, vi), hess(i, j));
              }
          });
      return;
    }
    template <typename Model>
    void
    computeElasticGradientAndHessian(zs::CudaExecutionPolicy &cudaPol,
                                     const Model &model,
                                     const zs::SmallString &gTag = "grad") {
      using namespace zs;
      constexpr auto space = execspace_e::cuda;
      for (auto &primHandle : prims)
        if (primHandle.category == ZenoParticles::surface)
          cudaPol(zs::range(primHandle.getEles().size()),
                  [vtemp = proxy<space>({}, vtemp),
                   etemp = proxy<space>({}, primHandle.etemp),
                   eles = proxy<space>({}, primHandle.getEles()), model, gTag,
                   dt = this->dt, projectDBC = projectDBC,
                   vOffset = primHandle.vOffset] __device__(int ei) mutable {
                    auto IB = eles.template pack<2, 2>("IB", ei);
                    auto inds = eles.template pack<3>("inds", ei)
                                    .template reinterpret_bits<int>() +
                                vOffset;
                    auto vole = eles("vol", ei);
                    vec3 xs[3] = {vtemp.template pack<3>("xn", inds[0]),
                                  vtemp.template pack<3>("xn", inds[1]),
                                  vtemp.template pack<3>("xn", inds[2])};
                    auto x1x0 = xs[1] - xs[0];
                    auto x2x0 = xs[2] - xs[0];

                    mat3 BCbasis[3];
                    int BCorder[3];
                    int BCfixed[3];
                    for (int i = 0; i != 3; ++i) {
                      BCbasis[i] = vtemp.pack<3, 3>("BCbasis", inds[i]);
                      BCorder[i] = vtemp("BCorder", inds[i]);
                      BCfixed[i] = vtemp("BCfixed", inds[i]);
                    }
                    zs::vec<T, 9, 9> H;
                    if (BCorder[0] == 3 && BCorder[1] == 3 && BCorder[2] == 3) {
                      etemp.tuple<9 * 9>("He", ei) = H.zeros();
                      return;
                    }

#if 0
      mat2 A{}, temp{};
      auto dA_div_dx = zs::vec<T, 4, 9>::zeros();
      A(0, 0) = x1x0.l2NormSqr();
      A(1, 0) = A(0, 1) = x1x0.dot(x2x0);
      A(1, 1) = x2x0.l2NormSqr();

      auto IA = inverse(A);
      auto lnJ = zs::log(determinant(A) * determinant(IB)) / 2;
      temp = -dt * dt * vole *
             (model.mu / 2 * IB + (-model.mu + model.lam * lnJ) / 2 * IA);
      for (int d = 0; d != 3; ++d) {
        dA_div_dx(0, d) = -2 * x1x0[d];
        dA_div_dx(0, 3 + d) = 2 * x1x0[d];
        dA_div_dx(1, d) = -x1x0[d] - x2x0[d];
        dA_div_dx(1, 3 + d) = x2x0[d];
        dA_div_dx(1, 6 + d) = x1x0[d];
        dA_div_dx(2, d) = -x1x0[d] - x2x0[d];
        dA_div_dx(2, 3 + d) = x2x0[d];
        dA_div_dx(2, 6 + d) = x1x0[d];
        dA_div_dx(3, d) = -2 * x2x0[d];
        dA_div_dx(3, 6 + d) = 2 * x2x0[d];
      }

      for (int i_ = 0; i_ != 3; ++i_) {
        auto vi = inds[i_];
        for (int d = 0; d != 3; ++d) {
          int i = i_ * 3 + d;
          atomic_add(
              exec_cuda, &vtemp(gTag, d, vi),
              dA_div_dx(0, i) * temp(0, 0) + dA_div_dx(1, i) * temp(1, 0) +
                  dA_div_dx(2, i) * temp(0, 1) + dA_div_dx(3, i) * temp(1, 1));
        }
      }

      // hessian rotation: trans^T hess * trans
      // left trans^T: multiplied on rows
      // right trans: multiplied on cols
      
      using mat9 = zs::vec<T, 9, 9>;
      mat9 ahess[4];
      for (int i = 0; i != 4; ++i)
        ahess[i] = mat9::zeros();
      for (int i = 0; i != 3; ++i) {
        ahess[3](i, i) = ahess[0](i, i) = 2;
        ahess[3](6 + i, 6 + i) = ahess[0](3 + i, 3 + i) = 2;
        ahess[3](i, 6 + i) = ahess[0](i, 3 + i) = -2;
        ahess[3](6 + i, i) = ahess[0](3 + i, i) = -2;
      }
      for (int i = 0; i != 3; ++i) {
        ahess[2](3 + i, 6 + i) = ahess[1](3 + i, 6 + i) = 1;
        ahess[2](6 + i, 3 + i) = ahess[1](6 + i, 3 + i) = 1;
        ahess[2](i, 3 + i) = ahess[1](i, 3 + i) = -1;
        ahess[2](i, 6 + i) = ahess[1](i, 6 + i) = -1;
        ahess[2](3 + i, i) = ahess[1](3 + i, i) = -1;
        ahess[2](6 + i, i) = ahess[1](6 + i, i) = -1;
        ahess[2](i, i) = ahess[1](i, i) = 2;
      }

      zs::vec<T, 9> ainvda;
      for (int i_ = 0; i_ < 3; ++i_) {
        for (int d = 0; d < 3; ++d) {
          int i = i_ * 3 + d;
          ainvda(i) = dA_div_dx(0, i) * IA(0, 0) + dA_div_dx(1, i) * IA(1, 0) +
                      dA_div_dx(2, i) * IA(0, 1) + dA_div_dx(3, i) * IA(1, 1);

          const T deta = determinant(A);
          const T lnJ = zs::log(deta * determinant(IB)) / 2;
          const T term1 = (-model.mu + model.lam * lnJ) / 2;
          H = (-term1 + model.lam / 4) * dyadic_prod(ainvda, ainvda);

          zs::vec<T, 4, 9> aderivadj;
          for (int d = 0; d != 9; ++d) {
            aderivadj(0, d) = dA_div_dx(3, d);
            aderivadj(1, d) = -dA_div_dx(1, d);
            aderivadj(2, d) = -dA_div_dx(2, d);
            aderivadj(3, d) = dA_div_dx(0, d);
          }
          H += term1 / deta * aderivadj.transpose() * dA_div_dx;

          for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j) {
              H += (term1 * IA(i, j) + model.mu / 2 * IB(i, j)) *
                   ahess[i + j * 2];
            }
        }
      }
      make_pd(H);
#else
              zs::vec<T, 3, 2> Ds{x1x0[0], x2x0[0], x1x0[1],
                                  x2x0[1], x1x0[2], x2x0[2]};
              auto F = Ds * IB;

              auto dFdX = dFdXMatrix(IB, wrapv<3>{});
              auto dFdXT = dFdX.transpose();
              auto f0 = col(F, 0);
              auto f1 = col(F, 1);
              auto f0Norm = zs::sqrt(f0.l2NormSqr());
              auto f1Norm = zs::sqrt(f1.l2NormSqr());
              auto f0Tf1 = f0.dot(f1);
              zs::vec<T, 3, 2> Pstretch, Pshear;
              for (int d = 0; d != 3; ++d) {
                Pstretch(d, 0) = 2 * (1 - 1 / f0Norm) * F(d, 0);
                Pstretch(d, 1) = 2 * (1 - 1 / f1Norm) * F(d, 1);
                Pshear(d, 0) = 2 * f0Tf1 * f1(d);
                Pshear(d, 1) = 2 * f0Tf1 * f0(d);
              }
              auto vecP = flatten(Pstretch + Pshear);
              auto vfdt2 = -vole * (dFdXT * vecP) * (model.mu * dt * dt);

              for (int i = 0; i != 3; ++i) {
                auto vi = inds[i];
                for (int d = 0; d != 3; ++d)
                  atomic_add(exec_cuda, &vtemp(gTag, d, vi),
                             (T)vfdt2(i * 3 + d));
              }

              /// ref: A Finite Element Formulation of Baraff-Witkin Cloth
              // suggested by huang kemeng
              auto stretchHessian = [&F, &model]() {
                auto H = zs::vec<T, 6, 6>::zeros();
                const zs::vec<T, 2> u{1, 0};
                const zs::vec<T, 2> v{0, 1};
                const T I5u = (F * u).l2NormSqr();
                const T I5v = (F * v).l2NormSqr();
                const T invSqrtI5u = (T)1 / zs::sqrt(I5u);
                const T invSqrtI5v = (T)1 / zs::sqrt(I5v);

                H(0, 0) = H(1, 1) = H(2, 2) = zs::max(1 - invSqrtI5u, (T)0);
                H(3, 3) = H(4, 4) = H(5, 5) = zs::max(1 - invSqrtI5v, (T)0);

                const auto fu = col(F, 0).normalized();
                const T uCoeff = (1 - invSqrtI5u >= 0) ? invSqrtI5u : (T)1;
                for (int i = 0; i != 3; ++i)
                  for (int j = 0; j != 3; ++j)
                    H(i, j) += uCoeff * fu(i) * fu(j);

                const auto fv = col(F, 1).normalized();
                const T vCoeff = (1 - invSqrtI5v >= 0) ? invSqrtI5v : (T)1;
                for (int i = 0; i != 3; ++i)
                  for (int j = 0; j != 3; ++j)
                    H(3 + i, 3 + j) += vCoeff * fv(i) * fv(j);

                H *= model.mu;
                return H;
              };
              auto shearHessian = [&F, &model]() {
                using mat6 = zs::vec<T, 6, 6>;
                auto H = mat6::zeros();
                const zs::vec<T, 2> u{1, 0};
                const zs::vec<T, 2> v{0, 1};
                const T I6 = (F * u).dot(F * v);
                const T signI6 = I6 >= 0 ? 1 : -1;

                H(3, 0) = H(4, 1) = H(5, 2) = H(0, 3) = H(1, 4) = H(2, 5) =
                    (T)1;

                const auto g_ = F * (dyadic_prod(u, v) + dyadic_prod(v, u));
                zs::vec<T, 6> g{};
                for (int j = 0, offset = 0; j != 2; ++j) {
                  for (int i = 0; i != 3; ++i)
                    g(offset++) = g_(i, j);
                }

                const T I2 = F.l2NormSqr();
                const T lambda0 =
                    (T)0.5 * (I2 + zs::sqrt(I2 * I2 + (T)12 * I6 * I6));

                const zs::vec<T, 6> q0 =
                    (I6 * H * g + lambda0 * g).normalized();

                auto t = mat6::identity();
                t = 0.5 * (t + signI6 * H);

                const zs::vec<T, 6> Tq = t * q0;
                const auto normTq = Tq.l2NormSqr();

                mat6 dPdF = zs::abs(I6) * (t - (dyadic_prod(Tq, Tq) / normTq)) +
                            lambda0 * (dyadic_prod(q0, q0));
                dPdF *= model.mu;
                return dPdF;
              };
              auto He = stretchHessian() + shearHessian();
              H = dFdX.transpose() * He * dFdX;
#endif
                    H *= dt * dt * vole;

                    // rotate and project
                    rotate_hessian(H, BCbasis, BCorder, BCfixed, projectDBC);
                    etemp.tuple<9 * 9>("He", ei) = H;
                    for (int vi = 0; vi != 3; ++vi) {
                      for (int i = 0; i != 3; ++i)
                        for (int j = 0; j != 3; ++j) {
                          atomic_add(exec_cuda,
                                     &vtemp("P", i * 3 + j, inds[vi]),
                                     H(vi * 3 + i, vi * 3 + j));
                        }
                    }
                  });
        else if (primHandle.category == ZenoParticles::tet)
          cudaPol(
              zs::range(primHandle.getEles().size()),
              [vtemp = proxy<space>({}, vtemp),
               etemp = proxy<space>({}, primHandle.etemp),
               eles = proxy<space>({}, primHandle.getEles()), model, gTag,
               dt = this->dt, projectDBC = projectDBC,
               vOffset = primHandle.vOffset] __device__(int ei) mutable {
                auto IB = eles.template pack<3, 3>("IB", ei);
                auto inds = eles.template pack<4>("inds", ei)
                                .template reinterpret_bits<int>() +
                            vOffset;
                auto vole = eles("vol", ei);
                vec3 xs[4] = {
                    vtemp.pack<3>("xn", inds[0]), vtemp.pack<3>("xn", inds[1]),
                    vtemp.pack<3>("xn", inds[2]), vtemp.pack<3>("xn", inds[3])};

                mat3 BCbasis[4];
                int BCorder[4];
                int BCfixed[4];
                for (int i = 0; i != 4; ++i) {
                  BCbasis[i] = vtemp.pack<3, 3>("BCbasis", inds[i]);
                  BCorder[i] = vtemp("BCorder", inds[i]);
                  BCfixed[i] = vtemp("BCfixed", inds[i]);
                }
                zs::vec<T, 12, 12> H;
                if (BCorder[0] == 3 && BCorder[1] == 3 && BCorder[2] == 3 &&
                    BCorder[3] == 3) {
                  etemp.tuple<12 * 12>("He", ei) = H.zeros();
                  return;
                }
                mat3 F{};
                {
                  auto x1x0 = xs[1] - xs[0];
                  auto x2x0 = xs[2] - xs[0];
                  auto x3x0 = xs[3] - xs[0];
                  auto Ds = mat3{x1x0[0], x2x0[0], x3x0[0], x1x0[1], x2x0[1],
                                 x3x0[1], x1x0[2], x2x0[2], x3x0[2]};
                  F = Ds * IB;
                }
                auto P = model.first_piola(F);
                auto vecP = flatten(P);
                auto dFdX = dFdXMatrix(IB);
                auto dFdXT = dFdX.transpose();
                auto vfdt2 = -vole * (dFdXT * vecP) * dt * dt;

                for (int i = 0; i != 4; ++i) {
                  auto vi = inds[i];
                  for (int d = 0; d != 3; ++d)
                    atomic_add(exec_cuda, &vtemp(gTag, d, vi),
                               (T)vfdt2(i * 3 + d));
                }

                auto Hq = model.first_piola_derivative(F, true_c);
                H = dFdXT * Hq * dFdX * vole * dt * dt;

                // rotate and project
                rotate_hessian(H, BCbasis, BCorder, BCfixed, projectDBC);
                etemp.tuple<12 * 12>("He", ei) = H;
                for (int vi = 0; vi != 4; ++vi) {
                  for (int i = 0; i != 3; ++i)
                    for (int j = 0; j != 3; ++j) {
                      atomic_add(exec_cuda, &vtemp("P", i * 3 + j, inds[vi]),
                                 H(vi * 3 + i, vi * 3 + j));
                    }
                }
              });
    }
    void computeInertialAndGravityPotentialGradient(
        zs::CudaExecutionPolicy &cudaPol,
        const zs::SmallString &gTag = "grad") {
      using namespace zs;
      constexpr auto space = execspace_e::cuda;
      for (auto &primHandle : prims) {
        auto &verts = primHandle.getVerts();
        cudaPol(range(verts.size()),
                [vtemp = proxy<space>({}, vtemp),
                 verts = proxy<space>({}, verts), gTag, extForce = extForce,
                 dt = dt,
                 vOffset = primHandle.vOffset] __device__(int i) mutable {
                  auto m = verts("m", i);
                  int BCorder = vtemp("BCorder", vOffset + i);
                  if (BCorder != 3) {
                    vtemp.tuple<3>(gTag, vOffset + i) =
                        m * extForce * dt * dt -
                        m * (vtemp.pack<3>("xn", vOffset + i) -
                             vtemp.pack<3>("xtilde", vOffset + i));
                  }
                  // should rotate here
                  vtemp("P", 0, vOffset + i) += m;
                  vtemp("P", 4, vOffset + i) += m;
                  vtemp("P", 8, vOffset + i) += m;
                });
      }
    }
#if 1
    template <typename Model>
    T energy(zs::CudaExecutionPolicy &pol, const Model &model,
             const zs::SmallString tag, bool includeAugLagEnergy = false) {
      using namespace zs;
      constexpr auto space = execspace_e::cuda;
      Vector<T> es{vtemp.get_allocator(), 1};
      // T E = 0;
      std::vector<T> Es(0);
      for (auto &primHandle : prims) {
        auto &verts = primHandle.getVerts();
        auto &eles = primHandle.getEles();
        es.resize(verts.size());
        pol(range(verts.size()),
            [verts = proxy<space>({}, verts), vtemp = proxy<space>({}, vtemp),
             es = proxy<space>(es), tag, extForce = extForce, dt = this->dt,
             vOffset = primHandle.vOffset] __device__(int vi) mutable {
              // inertia
              auto m = verts("m", vi);
              vi += vOffset;
              auto x = vtemp.pack<3>(tag, vi);
              int BCorder = vtemp("BCorder", vi);
              if (BCorder != 3) {
#if 0
                atomic_add(exec_cuda, &res[0],
                           (T)0.5 * m *
                               (x - vtemp.pack<3>("xtilde", vi)).l2NormSqr());
                // gravity
                atomic_add(exec_cuda, &res[0],
                           -m * extForce.dot(x - vtemp.pack<3>("xt", vi)) * dt *
                               dt);
#else
                es[vi] = (T)0.5 * m * (x - vtemp.pack<3>("xtilde", vi)).l2NormSqr() + 
                  -m * extForce.dot(x) * dt * dt;
#endif
              }
            });
        Es.push_back(reduce(pol, es));
        // E += reduce(pol, es);

        es.resize(eles.size());
        if (primHandle.category == ZenoParticles::surface) {
          // elasticity
          pol(range(eles.size()),
              [eles = proxy<space>({}, eles), vtemp = proxy<space>({}, vtemp),
               es = proxy<space>(es), tag, model = model, dt = this->dt,
               vOffset = primHandle.vOffset] __device__(int ei) mutable {
                auto IB = eles.template pack<2, 2>("IB", ei);
                auto inds = eles.template pack<3>("inds", ei)
                                .template reinterpret_bits<int>() +
                            vOffset;

                int BCorder[3];
                for (int i = 0; i != 3; ++i)
                  BCorder[i] = vtemp("BCorder", inds[i]);
                if (BCorder[0] == 3 && BCorder[1] == 3 && BCorder[2] == 3)
                  return;

                auto vole = eles("vol", ei);
                vec3 xs[3] = {vtemp.template pack<3>(tag, inds[0]),
                              vtemp.template pack<3>(tag, inds[1]),
                              vtemp.template pack<3>(tag, inds[2])};
                mat2 A{};
                T E;
                auto x1x0 = xs[1] - xs[0];
                auto x2x0 = xs[2] - xs[0];
#if 0
        {
          A(0, 0) = x1x0.l2NormSqr();
          A(1, 0) = A(0, 1) = x1x0.dot(x2x0);
          A(1, 1) = x2x0.l2NormSqr();

          auto IA = inverse(A);
          auto lnJ = zs::log(determinant(A) * determinant(IB)) / 2;
          E = dt * dt * vole *
              (model.mu / 2 * (trace(IB * A) - 2 - 2 * lnJ) +
               model.lam / 2 * lnJ * lnJ);
        }
#else
        zs::vec<T, 3, 2> Ds{x1x0[0], x2x0[0], x1x0[1], x2x0[1], x1x0[2], x2x0[2]};
        auto F = Ds * IB;
        auto f0 = col(F, 0);
        auto f1 = col(F, 1);
        auto f0Norm = zs::sqrt(f0.l2NormSqr());
        auto f1Norm = zs::sqrt(f1.l2NormSqr());
        auto Estretch = dt * dt * model.mu *vole * (zs::sqr(f0Norm - 1) + zs::sqr(f1Norm - 1));
        auto Eshear = dt * dt * model.mu *vole * zs::sqr(f0.dot(f1));
        E = Estretch + Eshear;
#endif
                // atomic_add(exec_cuda, &res[0], E);
                es[ei] = E;
              });
          Es.push_back(reduce(pol, es));
          // E += reduce(pol, es);
        } else if (primHandle.category == ZenoParticles::tet) {
          pol(zs::range(eles.size()),
              [vtemp = proxy<space>({}, vtemp), eles = proxy<space>({}, eles),
               es = proxy<space>(es), model, dt = this->dt,
               vOffset = primHandle.vOffset] __device__(int ei) mutable {
                auto IB = eles.template pack<3, 3>("IB", ei);
                auto inds = eles.template pack<4>("inds", ei)
                                .template reinterpret_bits<int>() +
                            vOffset;
                auto vole = eles("vol", ei);
                vec3 xs[4] = {
                    vtemp.pack<3>("xn", inds[0]), vtemp.pack<3>("xn", inds[1]),
                    vtemp.pack<3>("xn", inds[2]), vtemp.pack<3>("xn", inds[3])};

                int BCorder[4];
                for (int i = 0; i != 4; ++i)
                  BCorder[i] = vtemp("BCorder", inds[i]);
                if (BCorder[0] == 3 && BCorder[1] == 3 && BCorder[2] == 3 &&
                    BCorder[3] == 3)
                  return;

                mat3 F{};
                {
                  auto x1x0 = xs[1] - xs[0];
                  auto x2x0 = xs[2] - xs[0];
                  auto x3x0 = xs[3] - xs[0];
                  auto Ds = mat3{x1x0[0], x2x0[0], x3x0[0], x1x0[1], x2x0[1],
                                 x3x0[1], x1x0[2], x2x0[2], x3x0[2]};
                  F = Ds * IB;
                }
                // atomic_add(exec_cuda, &res[0], model.psi(F) * dt * dt *
                // vole);
                es[ei] = model.psi(F) * dt * dt * vole;
              });
          Es.push_back(reduce(pol, es));
          // E += reduce(pol, es);
        }
      }
      // contacts
      {
#if s_enableContact
        {
          auto activeGap2 = dHat * dHat + 2 * xi * dHat;
          auto numPP = nPP.getVal();
          es.resize(numPP);
          pol(range(numPP),
              [vtemp = proxy<space>({}, vtemp),
               tempPP = proxy<space>({}, tempPP), PP = proxy<space>(PP),
               es = proxy<space>(es), xi2 = xi * xi, dHat = dHat, activeGap2,
               kappa = kappa] __device__(int ppi) mutable {
                auto pp = PP[ppi];
                auto x0 = vtemp.pack<3>("xn", pp[0]);
                auto x1 = vtemp.pack<3>("xn", pp[1]);
                auto dist2 = dist2_pp(x0, x1);
                if (dist2 < xi2)
                  printf("dist already smaller than xi!\n");
                // atomic_add(exec_cuda, &res[0],
                //           zs::barrier(dist2 - xi2, activeGap2, kappa));
                es[ppi] = zs::barrier(dist2 - xi2, activeGap2, kappa);
              });
          Es.push_back(reduce(pol, es));
          // E += reduce(pol, es);

          auto numPE = nPE.getVal();
          es.resize(numPE);
          pol(range(numPE),
              [vtemp = proxy<space>({}, vtemp),
               tempPE = proxy<space>({}, tempPE), PE = proxy<space>(PE),
               es = proxy<space>(es), xi2 = xi * xi, dHat = dHat, activeGap2,
               kappa = kappa] __device__(int pei) mutable {
                auto pe = PE[pei];
                auto p = vtemp.pack<3>("xn", pe[0]);
                auto e0 = vtemp.pack<3>("xn", pe[1]);
                auto e1 = vtemp.pack<3>("xn", pe[2]);

                auto dist2 = dist2_pe(p, e0, e1);
                if (dist2 < xi2)
                  printf("dist already smaller than xi!\n");
                // atomic_add(exec_cuda, &res[0],
                //           zs::barrier(dist2 - xi2, activeGap2, kappa));
                es[pei] = zs::barrier(dist2 - xi2, activeGap2, kappa);
              });
          Es.push_back(reduce(pol, es));
          // E += reduce(pol, es);

          auto numPT = nPT.getVal();
          es.resize(numPT);
          pol(range(numPT),
              [vtemp = proxy<space>({}, vtemp),
               tempPT = proxy<space>({}, tempPT), PT = proxy<space>(PT),
               es = proxy<space>(es), xi2 = xi * xi, dHat = dHat, activeGap2,
               kappa = kappa] __device__(int pti) mutable {
                auto pt = PT[pti];
                auto p = vtemp.pack<3>("xn", pt[0]);
                auto t0 = vtemp.pack<3>("xn", pt[1]);
                auto t1 = vtemp.pack<3>("xn", pt[2]);
                auto t2 = vtemp.pack<3>("xn", pt[3]);

                auto dist2 = dist2_pt(p, t0, t1, t2);
                if (dist2 < xi2)
                  printf("dist already smaller than xi!\n");
                // atomic_add(exec_cuda, &res[0],
                //           zs::barrier(dist2 - xi2, activeGap2, kappa));
                es[pti] = zs::barrier(dist2 - xi2, activeGap2, kappa);
              });
          Es.push_back(reduce(pol, es));
          // E += reduce(pol, es);

          auto numEE = nEE.getVal();
          es.resize(numEE);
          pol(range(numEE),
              [vtemp = proxy<space>({}, vtemp),
               tempEE = proxy<space>({}, tempEE), EE = proxy<space>(EE),
               es = proxy<space>(es), xi2 = xi * xi, dHat = dHat, activeGap2,
               kappa = kappa] __device__(int eei) mutable {
                auto ee = EE[eei];
                auto ea0 = vtemp.pack<3>("xn", ee[0]);
                auto ea1 = vtemp.pack<3>("xn", ee[1]);
                auto eb0 = vtemp.pack<3>("xn", ee[2]);
                auto eb1 = vtemp.pack<3>("xn", ee[3]);

                auto dist2 = dist2_ee(ea0, ea1, eb0, eb1);
                if (dist2 < xi2)
                  printf("dist already smaller than xi!\n");
                // atomic_add(exec_cuda, &res[0],
                //           zs::barrier(dist2 - xi2, activeGap2, kappa));
                es[eei] = zs::barrier(dist2 - xi2, activeGap2, kappa);
              });
          Es.push_back(reduce(pol, es));
          // E += reduce(pol, es);
        }
#endif
        // boundary
        es.resize(coOffset);
        pol(range(coOffset),
            [vtemp = proxy<space>({}, vtemp), es = proxy<space>(es),
             gn = s_groundNormal, dHat2 = dHat * dHat,
             kappa = kappa] ZS_LAMBDA(int vi) mutable {
              auto x = vtemp.pack<3>("xn", vi);
              auto dist = gn.dot(x);
              auto dist2 = dist * dist;
              if (dist2 < dHat2) {
                auto temp = -(dist2 - dHat2) * (dist2 - dHat2) *
                            zs::log(dist2 / dHat2) * kappa;
                // atomic_add(exec_cuda, &res[0], temp);
                es[vi] = temp;
              } else
                es[vi] = 0;
            });
        Es.push_back(reduce(pol, es));
        // E += reduce(pol, es);
      }
      // constraints
      if (includeAugLagEnergy) {
        computeConstraints(pol, tag);
        es.resize(numDofs);
        pol(range(numDofs),
            [vtemp = proxy<space>({}, vtemp), es = proxy<space>(es),
             kappa = kappa] __device__(int vi) mutable {
              // already updated during "xn" update
              auto cons = vtemp.template pack<3>("cons", vi);
              auto w = vtemp("ws", vi);
              auto lambda = vtemp.pack<3>("lambda", vi);
#if 0
          atomic_add(
              exec_cuda, &res[0],
              (T)(-lambda.dot(cons) * w + 0.5 * kappa * w * cons.l2NormSqr()));
#else
              es[vi] = (T)(-lambda.dot(cons) * w + 0.5 * kappa * w * cons.l2NormSqr());
#endif
            });
        Es.push_back(reduce(pol, es));
        // E += reduce(pol, es);
      }
      std::sort(Es.begin(), Es.end());
      T E = 0;
      for (auto e : Es)
        E += e;
      return E;
    }
#else
    template <typename Model>
    T energy(zs::CudaExecutionPolicy &pol, const Model &model,
             const zs::SmallString tag, bool includeAugLagEnergy = false) {
      using namespace zs;
      constexpr auto space = execspace_e::cuda;
      Vector<T> res{vtemp.get_allocator(), 1};
      res.setVal(0);
      for (auto &primHandle : prims) {
        auto &verts = primHandle.getVerts();
        auto &eles = primHandle.getEles();
        pol(range(verts.size()),
            [verts = proxy<space>({}, verts), vtemp = proxy<space>({}, vtemp),
             res = proxy<space>(res), tag, extForce = extForce, dt = this->dt,
             vOffset = primHandle.vOffset] __device__(int vi) mutable {
              // inertia
              auto m = verts("m", vi);
              vi += vOffset;
              auto x = vtemp.pack<3>(tag, vi);
              int BCorder = vtemp("BCorder", vi);
              if (BCorder != 3) {
                atomic_add(exec_cuda, &res[0],
                           (T)0.5 * m *
                               (x - vtemp.pack<3>("xtilde", vi)).l2NormSqr());
                // gravity
                atomic_add(exec_cuda, &res[0], -m * extForce.dot(x) * dt * dt);
              }
            });
        if (primHandle.category == ZenoParticles::surface)
          // elasticity
          pol(range(eles.size()),
              [eles = proxy<space>({}, eles), vtemp = proxy<space>({}, vtemp),
               res = proxy<space>(res), tag, model = model, dt = this->dt,
               vOffset = primHandle.vOffset] __device__(int ei) mutable {
                auto IB = eles.template pack<2, 2>("IB", ei);
                auto inds = eles.template pack<3>("inds", ei)
                                .template reinterpret_bits<int>() +
                            vOffset;

                int BCorder[3];
                for (int i = 0; i != 3; ++i)
                  BCorder[i] = vtemp("BCorder", inds[i]);
                if (BCorder[0] == 3 && BCorder[1] == 3 && BCorder[2] == 3)
                  return;

                auto vole = eles("vol", ei);
                vec3 xs[3] = {vtemp.template pack<3>(tag, inds[0]),
                              vtemp.template pack<3>(tag, inds[1]),
                              vtemp.template pack<3>(tag, inds[2])};
                mat2 A{};
                T E;
                auto x1x0 = xs[1] - xs[0];
                auto x2x0 = xs[2] - xs[0];
#if 0
        {
          A(0, 0) = x1x0.l2NormSqr();
          A(1, 0) = A(0, 1) = x1x0.dot(x2x0);
          A(1, 1) = x2x0.l2NormSqr();

          auto IA = inverse(A);
          auto lnJ = zs::log(determinant(A) * determinant(IB)) / 2;
          E = dt * dt * vole *
              (model.mu / 2 * (trace(IB * A) - 2 - 2 * lnJ) +
               model.lam / 2 * lnJ * lnJ);
        }
#else
        zs::vec<T, 3, 2> Ds{x1x0[0], x2x0[0], x1x0[1], x2x0[1], x1x0[2], x2x0[2]};
        auto F = Ds * IB;
        auto f0 = col(F, 0);
        auto f1 = col(F, 1);
        auto f0Norm = zs::sqrt(f0.l2NormSqr());
        auto f1Norm = zs::sqrt(f1.l2NormSqr());
        auto Estretch = dt * dt * model.mu *vole * (zs::sqr(f0Norm - 1) + zs::sqr(f1Norm - 1));
        auto Eshear = dt * dt * model.mu *vole * zs::sqr(f0.dot(f1));
        E = Estretch + Eshear;
#endif
                atomic_add(exec_cuda, &res[0], E);
              });
        else if (primHandle.category == ZenoParticles::tet)
          pol(zs::range(eles.size()),
              [vtemp = proxy<space>({}, vtemp), eles = proxy<space>({}, eles),
               res = proxy<space>(res), model, dt = this->dt,
               vOffset = primHandle.vOffset] __device__(int ei) mutable {
                auto IB = eles.template pack<3, 3>("IB", ei);
                auto inds = eles.template pack<4>("inds", ei)
                                .template reinterpret_bits<int>() +
                            vOffset;
                auto vole = eles("vol", ei);
                vec3 xs[4] = {
                    vtemp.pack<3>("xn", inds[0]), vtemp.pack<3>("xn", inds[1]),
                    vtemp.pack<3>("xn", inds[2]), vtemp.pack<3>("xn", inds[3])};

                int BCorder[4];
                for (int i = 0; i != 4; ++i)
                  BCorder[i] = vtemp("BCorder", inds[i]);
                if (BCorder[0] == 3 && BCorder[1] == 3 && BCorder[2] == 3 &&
                    BCorder[3] == 3)
                  return;

                mat3 F{};
                {
                  auto x1x0 = xs[1] - xs[0];
                  auto x2x0 = xs[2] - xs[0];
                  auto x3x0 = xs[3] - xs[0];
                  auto Ds = mat3{x1x0[0], x2x0[0], x3x0[0], x1x0[1], x2x0[1],
                                 x3x0[1], x1x0[2], x2x0[2], x3x0[2]};
                  F = Ds * IB;
                }
                atomic_add(exec_cuda, &res[0], model.psi(F) * dt * dt * vole);
              });
      }
      // contacts
      {
#if s_enableContact
        {
          auto activeGap2 = dHat * dHat + 2 * xi * dHat;
          auto numPP = nPP.getVal();
          pol(range(numPP),
              [vtemp = proxy<space>({}, vtemp),
               tempPP = proxy<space>({}, tempPP), PP = proxy<space>(PP),
               res = proxy<space>(res), xi2 = xi * xi, dHat = dHat, activeGap2,
               kappa = kappa] __device__(int ppi) mutable {
                auto pp = PP[ppi];
                auto x0 = vtemp.pack<3>("xn", pp[0]);
                auto x1 = vtemp.pack<3>("xn", pp[1]);
                auto dist2 = dist2_pp(x0, x1);
                if (dist2 < xi2)
                  printf("dist already smaller than xi!\n");
                atomic_add(exec_cuda, &res[0],
                           zs::barrier(dist2 - xi2, activeGap2, kappa));
              });
          auto numPE = nPE.getVal();
          pol(range(numPE),
              [vtemp = proxy<space>({}, vtemp),
               tempPE = proxy<space>({}, tempPE), PE = proxy<space>(PE),
               res = proxy<space>(res), xi2 = xi * xi, dHat = dHat, activeGap2,
               kappa = kappa] __device__(int pei) mutable {
                auto pe = PE[pei];
                auto p = vtemp.pack<3>("xn", pe[0]);
                auto e0 = vtemp.pack<3>("xn", pe[1]);
                auto e1 = vtemp.pack<3>("xn", pe[2]);

                auto dist2 = dist2_pe(p, e0, e1);
                if (dist2 < xi2)
                  printf("dist already smaller than xi!\n");
                atomic_add(exec_cuda, &res[0],
                           zs::barrier(dist2 - xi2, activeGap2, kappa));
              });
          auto numPT = nPT.getVal();
          pol(range(numPT),
              [vtemp = proxy<space>({}, vtemp),
               tempPT = proxy<space>({}, tempPT), PT = proxy<space>(PT),
               res = proxy<space>(res), xi2 = xi * xi, dHat = dHat, activeGap2,
               kappa = kappa] __device__(int pti) mutable {
                auto pt = PT[pti];
                auto p = vtemp.pack<3>("xn", pt[0]);
                auto t0 = vtemp.pack<3>("xn", pt[1]);
                auto t1 = vtemp.pack<3>("xn", pt[2]);
                auto t2 = vtemp.pack<3>("xn", pt[3]);

                auto dist2 = dist2_pt(p, t0, t1, t2);
                if (dist2 < xi2)
                  printf("dist already smaller than xi!\n");
                atomic_add(exec_cuda, &res[0],
                           zs::barrier(dist2 - xi2, activeGap2, kappa));
              });
          auto numEE = nEE.getVal();
          pol(range(numEE),
              [vtemp = proxy<space>({}, vtemp),
               tempEE = proxy<space>({}, tempEE), EE = proxy<space>(EE),
               res = proxy<space>(res), xi2 = xi * xi, dHat = dHat, activeGap2,
               kappa = kappa] __device__(int eei) mutable {
                auto ee = EE[eei];
                auto ea0 = vtemp.pack<3>("xn", ee[0]);
                auto ea1 = vtemp.pack<3>("xn", ee[1]);
                auto eb0 = vtemp.pack<3>("xn", ee[2]);
                auto eb1 = vtemp.pack<3>("xn", ee[3]);

                auto dist2 = dist2_ee(ea0, ea1, eb0, eb1);
                if (dist2 < xi2)
                  printf("dist already smaller than xi!\n");
                atomic_add(exec_cuda, &res[0],
                           zs::barrier(dist2 - xi2, activeGap2, kappa));
              });
        }
#endif
        // boundary
        pol(range(coOffset),
            [vtemp = proxy<space>({}, vtemp), res = proxy<space>(res),
             gn = s_groundNormal, dHat2 = dHat * dHat,
             kappa = kappa] ZS_LAMBDA(int vi) mutable {
              auto x = vtemp.pack<3>("xn", vi);
              auto dist = gn.dot(x);
              auto dist2 = dist * dist;
              if (dist2 < dHat2) {
                auto temp = -(dist2 - dHat2) * (dist2 - dHat2) *
                            zs::log(dist2 / dHat2) * kappa;
                atomic_add(exec_cuda, &res[0], temp);
              }
            });
      }
      // constraints
      if (includeAugLagEnergy) {
        computeConstraints(pol, tag);
        pol(range(numDofs), [vtemp = proxy<space>({}, vtemp),
                             res = proxy<space>(res),
                             kappa = kappa] __device__(int vi) mutable {
          // already updated during "xn" update
          auto cons = vtemp.template pack<3>("cons", vi);
          auto w = vtemp("ws", vi);
          auto lambda = vtemp.pack<3>("lambda", vi);
          atomic_add(
              exec_cuda, &res[0],
              (T)(-lambda.dot(cons) * w + 0.5 * kappa * w * cons.l2NormSqr()));
        });
      }
      return res.getVal();
    }
#endif
    void project(zs::CudaExecutionPolicy &pol, const zs::SmallString tag) {
      using namespace zs;
      constexpr execspace_e space = execspace_e::cuda;
      // projection
      pol(zs::range(numDofs),
          [vtemp = proxy<space>({}, vtemp), projectDBC = projectDBC,
           tag] ZS_LAMBDA(int vi) mutable {
            int BCfixed = vtemp("BCfixed", vi);
            if (projectDBC || (!projectDBC && BCfixed)) {
              int BCorder = vtemp("BCorder", vi);
              for (int d = 0; d != BCorder; ++d)
                vtemp(tag, d, vi) = 0;
            }
          });
    }
    void precondition(zs::CudaExecutionPolicy &pol,
                      const zs::SmallString srcTag,
                      const zs::SmallString dstTag) {
      using namespace zs;
      constexpr execspace_e space = execspace_e::cuda;
      // precondition
      pol(zs::range(numDofs), [vtemp = proxy<space>({}, vtemp), srcTag,
                               dstTag] ZS_LAMBDA(int vi) mutable {
        vtemp.template tuple<3>(dstTag, vi) =
            vtemp.template pack<3, 3>("P", vi) *
            vtemp.template pack<3>(srcTag, vi);
      });
    }
    void multiply(zs::CudaExecutionPolicy &pol, const zs::SmallString dxTag,
                  const zs::SmallString bTag) {
      using namespace zs;
      constexpr execspace_e space = execspace_e::cuda;
      constexpr auto execTag = wrapv<space>{};
      // hessian rotation: trans^T hess * trans
      // left trans^T: multiplied on rows
      // right trans: multiplied on cols
      // dx -> b
      pol(range(numDofs), [execTag, vtemp = proxy<space>({}, vtemp),
                           bTag] ZS_LAMBDA(int vi) mutable {
        vtemp.template tuple<3>(bTag, vi) = vec3::zeros();
      });
      for (auto &primHandle : prims) {
        auto &verts = primHandle.getVerts();
        auto &eles = primHandle.getEles();
        // inertial
        pol(range(verts.size()),
            [execTag, verts = proxy<space>({}, verts),
             vtemp = proxy<space>({}, vtemp), dxTag, bTag,
             vOffset = primHandle.vOffset] ZS_LAMBDA(int vi) mutable {
              auto m = verts("m", vi);
              vi += vOffset;
              auto dx = vtemp.template pack<3>(dxTag, vi);
              auto BCbasis = vtemp.template pack<3, 3>("BCbasis", vi);
              int BCorder = vtemp("BCorder", vi);
              // dx = BCbasis.transpose() * m * BCbasis * dx;
              auto M = mat3::identity() * m;
              M = BCbasis.transpose() * M * BCbasis;
              for (int i = 0; i != BCorder; ++i)
                for (int j = 0; j != BCorder; ++j)
                  M(i, j) = (i == j ? 1 : 0);
              dx = M * dx;
              for (int d = 0; d != 3; ++d)
                atomic_add(execTag, &vtemp(bTag, d, vi), dx(d));
            });
        // elasticity
        if (primHandle.category == ZenoParticles::surface)
          pol(range(eles.size()),
              [execTag, etemp = proxy<space>({}, primHandle.etemp),
               vtemp = proxy<space>({}, vtemp), eles = proxy<space>({}, eles),
               dxTag, bTag,
               vOffset = primHandle.vOffset] ZS_LAMBDA(int ei) mutable {
                constexpr int dim = 3;
                auto inds = eles.template pack<3>("inds", ei)
                                .template reinterpret_bits<int>() +
                            vOffset;
                zs::vec<T, 3 * dim> temp{};
                for (int vi = 0; vi != 3; ++vi)
                  for (int d = 0; d != dim; ++d) {
                    temp[vi * dim + d] = vtemp(dxTag, d, inds[vi]);
                  }
                auto He = etemp.template pack<dim * 3, dim * 3>("He", ei);

                temp = He * temp;

                for (int vi = 0; vi != 3; ++vi)
                  for (int d = 0; d != dim; ++d) {
                    atomic_add(execTag, &vtemp(bTag, d, inds[vi]),
                               temp[vi * dim + d]);
                  }
              });
        else if (primHandle.category == ZenoParticles::tet)
          pol(range(eles.size()),
              [execTag, etemp = proxy<space>({}, primHandle.etemp),
               vtemp = proxy<space>({}, vtemp), eles = proxy<space>({}, eles),
               dxTag, bTag,
               vOffset = primHandle.vOffset] ZS_LAMBDA(int ei) mutable {
                constexpr int dim = 3;
                auto inds = eles.template pack<4>("inds", ei)
                                .template reinterpret_bits<int>() +
                            vOffset;
                zs::vec<T, 4 * dim> temp{};
                for (int vi = 0; vi != 4; ++vi)
                  for (int d = 0; d != dim; ++d) {
                    temp[vi * dim + d] = vtemp(dxTag, d, inds[vi]);
                  }
                auto He = etemp.template pack<dim * 4, dim * 4>("He", ei);

                temp = He * temp;

                for (int vi = 0; vi != 4; ++vi)
                  for (int d = 0; d != dim; ++d) {
                    atomic_add(execTag, &vtemp(bTag, d, inds[vi]),
                               temp[vi * dim + d]);
                  }
              });
      }
      // contacts
      {
#if s_enableContact
        {
          auto numPP = nPP.getVal();
          pol(range(numPP), [execTag, tempPP = proxy<space>({}, tempPP),
                             vtemp = proxy<space>({}, vtemp), dxTag, bTag,
                             PP = proxy<space>(PP)] ZS_LAMBDA(int ppi) mutable {
            constexpr int dim = 3;
            auto pp = PP[ppi];
            zs::vec<T, dim * 2> temp{};
            for (int vi = 0; vi != 2; ++vi)
              for (int d = 0; d != dim; ++d) {
                temp[vi * dim + d] = vtemp(dxTag, d, pp[vi]);
              }
            auto ppHess = tempPP.template pack<6, 6>("H", ppi);

            temp = ppHess * temp;

            for (int vi = 0; vi != 2; ++vi)
              for (int d = 0; d != dim; ++d) {
                atomic_add(execTag, &vtemp(bTag, d, pp[vi]),
                           temp[vi * dim + d]);
              }
          });
          auto numPE = nPE.getVal();
          pol(range(numPE), [execTag, tempPE = proxy<space>({}, tempPE),
                             vtemp = proxy<space>({}, vtemp), dxTag, bTag,
                             PE = proxy<space>(PE)] ZS_LAMBDA(int pei) mutable {
            constexpr int dim = 3;
            auto pe = PE[pei];
            zs::vec<T, dim * 3> temp{};
            for (int vi = 0; vi != 3; ++vi)
              for (int d = 0; d != dim; ++d) {
                temp[vi * dim + d] = vtemp(dxTag, d, pe[vi]);
              }
            auto peHess = tempPE.template pack<9, 9>("H", pei);

            temp = peHess * temp;

            for (int vi = 0; vi != 3; ++vi)
              for (int d = 0; d != dim; ++d) {
                atomic_add(execTag, &vtemp(bTag, d, pe[vi]),
                           temp[vi * dim + d]);
              }
          });
          auto numPT = nPT.getVal();
          pol(range(numPT), [execTag, tempPT = proxy<space>({}, tempPT),
                             vtemp = proxy<space>({}, vtemp), dxTag, bTag,
                             PT = proxy<space>(PT)] ZS_LAMBDA(int pti) mutable {
            constexpr int dim = 3;
            auto pt = PT[pti];
            zs::vec<T, dim * 4> temp{};
            for (int vi = 0; vi != 4; ++vi)
              for (int d = 0; d != dim; ++d) {
                temp[vi * dim + d] = vtemp(dxTag, d, pt[vi]);
              }
            auto ptHess = tempPT.template pack<12, 12>("H", pti);

            temp = ptHess * temp;

            for (int vi = 0; vi != 4; ++vi)
              for (int d = 0; d != dim; ++d) {
                atomic_add(execTag, &vtemp(bTag, d, pt[vi]),
                           temp[vi * dim + d]);
              }
          });
          auto numEE = nEE.getVal();
          pol(range(numEE), [execTag, tempEE = proxy<space>({}, tempEE),
                             vtemp = proxy<space>({}, vtemp), dxTag, bTag,
                             EE = proxy<space>(EE)] ZS_LAMBDA(int eei) mutable {
            constexpr int dim = 3;
            auto ee = EE[eei];
            zs::vec<T, dim * 4> temp{};
            for (int vi = 0; vi != 4; ++vi)
              for (int d = 0; d != dim; ++d) {
                temp[vi * dim + d] = vtemp(dxTag, d, ee[vi]);
              }
            auto eeHess = tempEE.template pack<12, 12>("H", eei);

            temp = eeHess * temp;

            for (int vi = 0; vi != 4; ++vi)
              for (int d = 0; d != dim; ++d) {
                atomic_add(execTag, &vtemp(bTag, d, ee[vi]),
                           temp[vi * dim + d]);
              }
          });
        }
#endif
        // boundary
        pol(range(coOffset), [execTag, vtemp = proxy<space>({}, vtemp),
                              tempPB = proxy<space>({}, tempPB), dxTag,
                              bTag] ZS_LAMBDA(int vi) mutable {
          auto dx = vtemp.template pack<3>(dxTag, vi);
          auto pbHess = tempPB.template pack<3, 3>("H", vi);
          dx = pbHess * dx;
          for (int d = 0; d != 3; ++d)
            atomic_add(execTag, &vtemp(bTag, d, vi), dx(d));
        });
      } // end contacts

      // constraint hessian
      if (!BCsatisfied) {
        pol(range(numDofs), [execTag, vtemp = proxy<space>({}, vtemp), dxTag,
                             bTag, kappa = kappa] ZS_LAMBDA(int vi) mutable {
          auto cons = vtemp.template pack<3>("cons", vi);
          auto dx = vtemp.template pack<3>(dxTag, vi);
          auto w = vtemp("ws", vi);
          for (int d = 0; d != 3; ++d)
            if (cons[d] != 0)
              atomic_add(execTag, &vtemp(bTag, d, vi), kappa * w * dx(d));
        });
      }
    }
    void initialize(zs::CudaExecutionPolicy &pol) {
      using namespace zs;
      constexpr auto space = execspace_e::cuda;
      stInds = tiles_t{vtemp.get_allocator(), {{"inds", 3}}, sfOffset};
      seInds = tiles_t{vtemp.get_allocator(), {{"inds", 2}}, seOffset};
      svInds = tiles_t{vtemp.get_allocator(), {{"inds", 1}}, svOffset};
      for (auto &primHandle : prims) {
        auto &verts = primHandle.getVerts();
        // initialize BC info
        // predict pos, initialize augmented lagrangian, constrain weights
        pol(Collapse(verts.size()),
            [vtemp = proxy<space>({}, vtemp), verts = proxy<space>({}, verts),
             voffset = primHandle.vOffset, dt = dt,
             extForce = extForce] __device__(int i) mutable {
              vtemp("BCorder", voffset + i) = verts("BCorder", i);
              vtemp.template tuple<9>("BCbasis", voffset + i) =
                  verts.template pack<3, 3>("BCbasis", i);
              vtemp.template tuple<3>("BCtarget", voffset + i) =
                  verts.template pack<3>("BCtarget", i);
              vtemp("BCfixed", voffset + i) = verts("BCfixed", i);

              auto x = verts.pack<3>("x", i);
              auto v = verts.pack<3>("v", i);
              vtemp.tuple<3>("xtilde", voffset + i) = x + v * dt;
              vtemp("ws", voffset + i) = zs::sqrt(verts("m", i));
              vtemp.tuple<3>("lambda", voffset + i) = vec3::zeros();
              vtemp.tuple<3>("xn", voffset + i) = x;
              vtemp.tuple<3>("xt", voffset + i) = x;
              vtemp.tuple<3>("x0", voffset + i) = verts.pack<3>("x0", i);
            });
        // record surface (tri) indices
        auto &tris = primHandle.getSurfTris();
        pol(Collapse(tris.size()),
            [stInds = proxy<space>({}, stInds), tris = proxy<space>({}, tris),
             voffset = primHandle.vOffset,
             sfoffset = primHandle.sfOffset] __device__(int i) mutable {
              stInds.template tuple<3>("inds", sfoffset + i) =
                  (tris.template pack<3>("inds", i)
                       .template reinterpret_bits<int>() +
                   (int)voffset)
                      .template reinterpret_bits<float>();
            });
        auto &edges = primHandle.getSurfEdges();
        pol(Collapse(edges.size()),
            [seInds = proxy<space>({}, seInds), edges = proxy<space>({}, edges),
             voffset = primHandle.vOffset,
             seoffset = primHandle.seOffset] __device__(int i) mutable {
              seInds.template tuple<2>("inds", seoffset + i) =
                  (edges.template pack<2>("inds", i)
                       .template reinterpret_bits<int>() +
                   (int)voffset)
                      .template reinterpret_bits<float>();
            });
        auto &points = primHandle.getSurfVerts();
        pol(Collapse(points.size()),
            [svInds = proxy<space>({}, svInds),
             points = proxy<space>({}, points), voffset = primHandle.vOffset,
             svoffset = primHandle.svOffset] __device__(int i) mutable {
              svInds("inds", svoffset + i) = reinterpret_bits<float>(
                  reinterpret_bits<int>(points("inds", i)) + (int)voffset);
            });
      }
      pol(Collapse(coVerts.size()),
          [vtemp = proxy<space>({}, vtemp), coverts = proxy<space>({}, coVerts),
           coOffset = coOffset, dt = dt,
           augLagCoeff = augLagCoeff] __device__(int i) mutable {
            auto x = coverts.pack<3>("x", i);
            vec3 newX{};
            if (coverts.hasProperty("BCtarget"))
              newX = coverts.pack<3>("BCtarget", i);
            else {
              auto v = coverts.pack<3>("v", i);
              newX = x + v * dt;
            }
            vtemp("BCorder", coOffset + i) = 3;
            vtemp.template tuple<9>("BCbasis", coOffset + i) = mat3::identity();
            vtemp.template tuple<3>("BCtarget", coOffset + i) = newX;
            vtemp("BCfixed", coOffset + i) =
                (newX - x).l2NormSqr() == 0 ? 1 : 0;

            vtemp.tuple<3>("xtilde", coOffset + i) = newX;
            // vtemp("ws", coOffset + i) = zs::sqrt(coverts("m", i) * 1e3);
            vtemp("ws", coOffset + i) = zs::sqrt(coverts("m", i) * augLagCoeff);
            vtemp.tuple<3>("lambda", coOffset + i) = vec3::zeros();
            vtemp.tuple<3>("xn", coOffset + i) = x;
            vtemp.tuple<3>("xt", coOffset + i) = x;
            vtemp.tuple<3>("x0", coOffset + i) = coverts.pack<3>("x0", i);
          });
    }
    void updateVelocities(zs::CudaExecutionPolicy &pol) {
      using namespace zs;
      constexpr auto space = execspace_e::cuda;
      for (auto &primHandle : prims) {
        auto &verts = primHandle.getVerts();
        // update velocity and positions
        pol(zs::range(verts.size()),
            [vtemp = proxy<space>({}, vtemp), verts = proxy<space>({}, verts),
             dt = dt, vOffset = primHandle.vOffset] __device__(int vi) mutable {
              auto newX = vtemp.pack<3>("xn", vOffset + vi);
              verts.tuple<3>("x", vi) = newX;
              auto dv = (newX - vtemp.pack<3>("xtilde", vOffset + vi)) / dt;
              auto vn = verts.pack<3>("v", vi);
              vn += dv;
              verts.tuple<3>("v", vi) = vn;
            });
      }
    }

    IPCSystem(std::vector<ZenoParticles *> zsprims, const dtiles_t &coVerts,
              const tiles_t &coEdges, const tiles_t &coEles, T dt,
              const ZenoConstitutiveModel &models)
        : coVerts{coVerts}, coEdges{coEdges}, coEles{coEles},
          PP{estNumCps, zs::memsrc_e::um, 0},
          nPP{zsprims[0]->getParticles<true>().get_allocator(), 1},
          tempPP{{{"H", 36}}, //, {"inds_pre", 2}, {"dist2_pre", 1}
                 estNumCps,
                 zs::memsrc_e::um,
                 0},
          PE{estNumCps, zs::memsrc_e::um, 0},
          nPE{zsprims[0]->getParticles<true>().get_allocator(), 1},
          tempPE{{{"H", 81}}, estNumCps, zs::memsrc_e::um, 0},
          PT{estNumCps, zs::memsrc_e::um, 0},
          nPT{zsprims[0]->getParticles<true>().get_allocator(), 1},
          tempPT{{{"H", 144}}, estNumCps, zs::memsrc_e::um, 0},
          EE{estNumCps, zs::memsrc_e::um, 0},
          nEE{zsprims[0]->getParticles<true>().get_allocator(), 1},
          tempEE{{{"H", 144}}, estNumCps, zs::memsrc_e::um, 0},
          csPT{estNumCps, zs::memsrc_e::um, 0}, csEE{estNumCps,
                                                     zs::memsrc_e::um, 0},
          ncsPT{zsprims[0]->getParticles<true>().get_allocator(), 1},
          ncsEE{zsprims[0]->getParticles<true>().get_allocator(), 1}, dt{dt},
          models{models} {
      coOffset = sfOffset = seOffset = svOffset = 0;
      for (auto primPtr : zsprims) {
        if (primPtr->category == ZenoParticles::category_e::surface)
          prims.emplace_back(*primPtr, coOffset, sfOffset, seOffset, svOffset,
                             zs::wrapv<3>{});
        else if (primPtr->category == ZenoParticles::category_e::tet)
          prims.emplace_back(*primPtr, coOffset, sfOffset, seOffset, svOffset,
                             zs::wrapv<4>{});
      }
      numDofs = coOffset + coVerts.size();
      vtemp = dtiles_t{
          zsprims[0]->getParticles<true>().get_allocator(),
          {{"grad", 3},
           {"P", 9},
           // dirichlet boundary condition type; 0: NOT, 1: ZERO, 2: NONZERO
           {"BCorder", 1},
           {"BCbasis", 9},
           {"BCtarget", 3},
           {"BCfixed", 1},
           {"ws", 1}, // also as constraint jacobian
           {"cons", 3},
           {"lambda", 3},

           {"dir", 3},
           {"xn", 3},
           {"x0", 3}, // initial positions
           {"xt", 3}, // initial positions at the current timestep
           {"xn0", 3},
           {"xtilde", 3},
           {"temp", 3},
           {"r", 3},
           {"p", 3},
           {"q", 3}},
          numDofs};
      // ground hessian
      tempPB = dtiles_t{vtemp.get_allocator(), {{"H", 9}}, coOffset};
      nPP.setVal(0);
      nPE.setVal(0);
      nPT.setVal(0);
      nEE.setVal(0);

      ncsPT.setVal(0);
      ncsEE.setVal(0);

      auto cudaPol = zs::cuda_exec();
      // average edge length (for CCD filtering)
      meanEdgeLength = averageSurfEdgeLength(cudaPol);
      initialize(cudaPol);
      fmt::print("num total obj <verts, surfV, surfE, surfT>: {}, {}, {}, {}\n",
                 coOffset, svOffset, seOffset, sfOffset);
      {
        {
          auto triBvs = retrieve_bounding_volumes(cudaPol, vtemp, "xn", stInds,
                                                  zs::wrapv<3>{}, 0);
          stBvh.build(cudaPol, triBvs);
          auto edgeBvs = retrieve_bounding_volumes(cudaPol, vtemp, "xn", seInds,
                                                   zs::wrapv<2>{}, 0);
          seBvh.build(cudaPol, edgeBvs);
        }
        {
          auto triBvs = retrieve_bounding_volumes(cudaPol, vtemp, "xn", coEles,
                                                  zs::wrapv<3>{}, coOffset);
          bouStBvh.build(cudaPol, triBvs);
          auto edgeBvs = retrieve_bounding_volumes(
              cudaPol, vtemp, "xn", coEdges, zs::wrapv<2>{}, coOffset);
          bouSeBvh.build(cudaPol, edgeBvs);
        }
      }
    }

    std::vector<PrimitiveHandle> prims;

    // (scripted) collision objects
    const dtiles_t &coVerts;
    const tiles_t &coEdges, &coEles;
    dtiles_t vtemp;
    // self contacts
    using pair_t = zs::vec<int, 2>;
    using pair3_t = zs::vec<int, 3>;
    using pair4_t = zs::vec<int, 4>;
    zs::Vector<pair_t> PP;
    zs::Vector<int> nPP;
    dtiles_t tempPP;
    zs::Vector<pair3_t> PE;
    zs::Vector<int> nPE;
    dtiles_t tempPE;
    zs::Vector<pair4_t> PT;
    zs::Vector<int> nPT;
    dtiles_t tempPT;
    zs::Vector<pair4_t> EE;
    zs::Vector<int> nEE;
    dtiles_t tempEE;

    zs::Vector<pair4_t> csPT, csEE;
    zs::Vector<int> ncsPT, ncsEE;

    // boundary contacts
    dtiles_t tempPB;
    // end contacts
    const ZenoConstitutiveModel &models;
    // auxiliary data (spatial acceleration)
    bvh_t stBvh, seBvh; // for simulated objects
    tiles_t stInds, seInds, svInds;
    std::size_t coOffset, numDofs;
    std::size_t sfOffset, seOffset, svOffset;
    bvh_t bouStBvh, bouSeBvh; // for collision objects
    T meanEdgeLength, dt;
  };

#if 1
  static T reduce(zs::CudaExecutionPolicy &cudaPol, const zs::Vector<T> &res) {
    using namespace zs;
    constexpr auto space = execspace_e::cuda;
    Vector<T> ret{res.get_allocator(), 1};
    auto sid = cudaPol.getStreamid();
    auto procid = cudaPol.getProcid();
    auto &context = Cuda::context(procid);
    auto stream = (cudaStream_t)context.streamSpare(sid);
    std::size_t temp_bytes = 0;
    cub::DeviceReduce::Reduce(nullptr, temp_bytes, res.data(), ret.data(),
                              res.size(), std::plus<T>{}, (T)0, stream);
    Vector<std::max_align_t> temp{res.get_allocator(),
                                  temp_bytes / sizeof(std::max_align_t) + 1};
    cub::DeviceReduce::Reduce(temp.data(), temp_bytes, res.data(), ret.data(),
                              res.size(), std::plus<T>{}, (T)0, stream);
    context.syncStreamSpare(sid);
    return (T)ret.getVal();
  }
#else
  static T reduce(zs::CudaExecutionPolicy &cudaPol, const zs::Vector<T> &res,
                  bool stable = false) {
    using namespace zs;
    auto sid = cudaPol.getStreamid();
    auto procid = cudaPol.getProcid();
    auto &context = Cuda::context(procid);
    auto stream = (cudaStream_t)context.streamSpare(sid);

    thrust::cuda::par.on(stream);
    if (stable) {
#if 1
      zs::Vector<T> ret{res.get_allocator(), 1};
      ret.setVal(0);
      thrust::inclusive_scan(
          thrust::device, thrust::device_pointer_cast(res.data()),
          thrust::device_pointer_cast(res.data() + res.size()),
          thrust::device_pointer_cast(ret.data()));
      return ret.getVal();
#else
      zs::inclusive_scan(cudaPol, std::begin(res), std::end(res),
                         std::begin(ret));
      return ret.getVal(ret.size() - 1);
#endif
    } else
      return thrust::reduce(
          thrust::device_pointer_cast(res.data()),
          thrust::device_pointer_cast(res.data() + res.size()));
  }
#endif
  static T dot(zs::CudaExecutionPolicy &cudaPol, dtiles_t &vertData,
               const zs::SmallString tag0, const zs::SmallString tag1) {
    using namespace zs;
    constexpr auto space = execspace_e::cuda;
    Vector<double> res{vertData.get_allocator(), vertData.size()};
    cudaPol(range(vertData.size()),
            [data = proxy<space>({}, vertData), res = proxy<space>(res), tag0,
             tag1] __device__(int pi) mutable {
              auto v0 = data.pack<3>(tag0, pi);
              auto v1 = data.pack<3>(tag1, pi);
              res[pi] = v0.dot(v1);
            });
    return reduce(cudaPol, res);
  }
  static T infNorm(zs::CudaExecutionPolicy &cudaPol, dtiles_t &vertData,
                   const zs::SmallString tag = "dir") {
    using namespace zs;
    constexpr auto space = execspace_e::cuda;
    Vector<T> ret{vertData.get_allocator(), 1};
    ret.setVal(0);
    cudaPol(range(vertData.size()),
            [data = proxy<space>({}, vertData), ret = proxy<space>(ret),
             tag] __device__(int pi) mutable {
              auto v = data.pack<3>(tag, pi);
              atomic_max(exec_cuda, &ret[0], v.abs().max());
            });
    return ret.getVal();
  }

  void apply() override {
    using namespace zs;
    constexpr auto space = execspace_e::cuda;
    auto cudaPol = cuda_exec().sync(true);

    auto zstets = RETRIEVE_OBJECT_PTRS(ZenoParticles, "ZSParticles");
    // auto zstets = get_input<ZenoParticles>("ZSParticles");
    std::shared_ptr<ZenoParticles> zsboundary;
    if (has_input<ZenoParticles>("ZSBoundaryPrimitives"))
      zsboundary = get_input<ZenoParticles>("ZSBoundaryPrimitives");
    auto models = zstets[0]->getModel();
    auto dt = get_input2<float>("dt");

    /// solver parameters
    auto input_est_num_cps = get_input2<int>("est_num_cps");
    auto input_dHat = get_input2<float>("dHat");
    auto input_kappa0 = get_input2<float>("kappa0");
    auto input_aug_coeff = get_input2<float>("aug_coeff");
    auto input_pn_rel = get_input2<float>("pn_rel");
    auto input_cg_rel = get_input2<float>("cg_rel");
    auto input_gravity = get_input2<float>("gravity");

    kappa0 = input_kappa0;
    augLagCoeff = input_aug_coeff;
    pnRel = input_pn_rel;
    cgRel = input_cg_rel;

    /// if there are no high precision verts, init from the low precision one
    for (auto zstet : zstets) {
      if (!zstet->hasImage(ZenoParticles::s_particleTag)) {
        auto &loVerts = zstet->getParticles();
        auto &verts = zstet->images[ZenoParticles::s_particleTag];
        verts = typename ZenoParticles::dtiles_t{
            loVerts.get_allocator(), loVerts.getPropertyTags(), loVerts.size()};
        cudaPol(range(verts.size()),
                [loVerts = proxy<space>({}, loVerts),
                 verts = proxy<space>({}, verts)] __device__(int vi) mutable {
                  // make sure there are no "inds"-like properties in verts!
                  for (int propid = 0; propid != verts._N; ++propid) {
                    auto propOffset = verts._tagOffsets[propid];
                    for (int chn = 0; chn != verts._tagSizes[propid]; ++chn)
                      verts(propOffset + chn, vi) =
                          loVerts(propOffset + chn, vi);
                  }
                });
      }
    }
    if (!zsboundary->hasImage(ZenoParticles::s_particleTag)) {
      auto &loVerts = zsboundary->getParticles();
      auto &verts = zsboundary->images[ZenoParticles::s_particleTag];
      verts = typename ZenoParticles::dtiles_t{
          loVerts.get_allocator(), loVerts.getPropertyTags(), loVerts.size()};
      cudaPol(range(verts.size()),
              [loVerts = proxy<space>({}, loVerts),
               verts = proxy<space>({}, verts)] __device__(int vi) mutable {
                // make sure there are no "inds"-like properties in verts!
                for (int propid = 0; propid != verts._N; ++propid) {
                  auto propOffset = verts._tagOffsets[propid];
                  for (int chn = 0; chn != verts._tagSizes[propid]; ++chn)
                    verts(propOffset + chn, vi) = loVerts(propOffset + chn, vi);
                }
              });
    }
    const dtiles_t &coVerts =
        zsboundary ? zsboundary->getParticles<true>() : dtiles_t{};
    const tiles_t &coEdges =
        zsboundary ? (*zsboundary)[ZenoParticles::s_surfEdgeTag] : tiles_t{};
    const tiles_t &coEles =
        zsboundary ? zsboundary->getQuadraturePoints() : tiles_t{};

    IPCSystem A{zstets, coVerts, coEdges, coEles, dt, models};

    auto coOffset = A.coOffset;
    auto numDofs = A.numDofs;

    estNumCps = input_est_num_cps > 0
                    ? input_est_num_cps // if specified, overwrite
                    : std::max(numDofs * 4, estNumCps);

    dtiles_t &vtemp = A.vtemp;

    /// time integrator
    dHat = input_dHat;
    extForce = vec3{0, input_gravity, 0};
    kappa = kappa0;
    targetGRes = pnRel;

    projectDBC = false;
    BCsatisfied = false;
    useGD = false;

#if s_enableAdaptiveSetting
    {
      A.updateWholeBoundingBoxSize(cudaPol);
      /// dHat
      dHat = 1e-3 * std::sqrt(boxDiagSize2);
      /// grad pn residual tolerance
      targetGRes = 1e-5 * std::sqrt(boxDiagSize2);
      /// mean mass
      avgNodeMass = 0;
      T sumNodeMass = 0;
      int sumNodes = 0;
      for (auto zstet : zstets) {
        sumNodes += zstet->numParticles();
        if (zstet->hasMeta(s_meanMassTag)) {
          // avgNodeMass = zstets[0]->readMeta(s_meanMassTag, wrapt<T>{});
          sumNodeMass += zstet->readMeta(s_meanMassTag, wrapt<T>{}) *
                         zstet->numParticles();
        } else {
          Vector<T> masses{vtemp.get_allocator(), zstet->numParticles()};
          cudaPol(Collapse{zstet->numParticles()},
                  [masses = proxy<space>(masses),
                   vtemp = proxy<space>({}, vtemp)] __device__(int vi) mutable {
                    masses[vi] = zs::sqr(vtemp("ws", vi));
                  });
          auto tmp = reduce(cudaPol, masses);
          sumNodeMass += tmp;
          zstet->setMeta(s_meanMassTag, tmp / zstet->numParticles());
        }
      }
      avgNodeMass = sumNodeMass / sumNodes;
      /// adaptive kappa
      {
        T H_b = computeHb((T)1e-16 * boxDiagSize2, dHat * dHat);
        kappaMin = 1e11 * avgNodeMass / (4e-16 * boxDiagSize2 * H_b);
        kappa = kappaMin;
        kappaMax = 100 * kappa;
      }
      fmt::print("auto dHat: {}, targetGRes: {}\n", dHat, targetGRes);
      fmt::print("average node mass: {}, kappa: {}\n", avgNodeMass, kappa);
      getchar();
    }
#endif

    /// optimizer
    for (int newtonIter = 0; newtonIter != 1000; ++newtonIter) {
      // check constraints
      if (!BCsatisfied) {
        A.computeConstraints(cudaPol, "xn");
        auto cr = A.constraintResidual(cudaPol, true);
        if (A.areConstraintsSatisfied(cudaPol)) {
          fmt::print("satisfied cons res [{}] at newton iter [{}]\n", cr,
                     newtonIter);
          // A.checkDBCStatus(cudaPol);
          // getchar();
          projectDBC = true;
          BCsatisfied = true;
        }
        fmt::print(fg(fmt::color::alice_blue),
                   "newton iter {} cons residual: {}\n", newtonIter, cr);
      }

      if constexpr (s_enableContact)
        A.findCollisionConstraints(cudaPol, dHat, xi);
      auto [npp, npe, npt, nee, ncspt, ncsee] = A.getCnts();
      // construct gradient, prepare hessian, prepare preconditioner
      cudaPol(zs::range(numDofs),
              [vtemp = proxy<space>({}, vtemp)] __device__(int i) mutable {
                vtemp.tuple<9>("P", i) = mat3::zeros();
                vtemp.tuple<3>("grad", i) = vec3::zeros();
              });
      A.computeInertialAndGravityPotentialGradient(cudaPol, "grad");
#if 0
      cudaPol(zs::range(coVerts.size()),
              [vtemp = proxy<space>({}, vtemp),
               coverts = proxy<space>({}, coVerts), coOffset,
               dt] __device__(int i) mutable {
                auto m = zs::sqr(vtemp("ws", coOffset + i));
                auto v = coverts.pack<3>("v", i);
                // only inertial, no extforce grad
                vtemp.tuple<3>("grad", coOffset + i) =
                    -m * (vtemp.pack<3>("xn", coOffset + i) -
                          vtemp.pack<3>("xtilde", coOffset + i));
              });
#endif
      match([&](auto &elasticModel) {
        A.computeElasticGradientAndHessian(cudaPol, elasticModel);
      })(models.getElasticModel());
      A.computeBoundaryBarrierGradientAndHessian(cudaPol);
      if constexpr (s_enableContact)
        A.computeBarrierGradientAndHessian(cudaPol);

      // rotate gradient and project
      cudaPol(zs::range(numDofs),
              [vtemp = proxy<space>({}, vtemp),
               projectDBC = projectDBC] __device__(int i) mutable {
                auto grad = vtemp.pack<3, 3>("BCbasis", i).transpose() *
                            vtemp.pack<3>("grad", i);
                int BCfixed = vtemp("BCfixed", i);
                if (projectDBC || BCfixed == 1) {
                  if (int BCorder = vtemp("BCorder", i); BCorder > 0)
                    for (int d = 0; d != BCorder; ++d)
                      grad(d) = 0;
                }
                vtemp.tuple<3>("grad", i) = grad;
              });
      // apply constraints (augmented lagrangians) after rotation!
      if (!BCsatisfied) {
        // grad
        cudaPol(zs::range(numDofs), [vtemp = proxy<space>({}, vtemp),
                                     kappa = kappa] __device__(int i) mutable {
          // computed during the previous constraint residual check
          auto cons = vtemp.pack<3>("cons", i);
          auto w = vtemp("ws", i);
          vtemp.tuple<3>("grad", i) = vtemp.pack<3>("grad", i) +
                                      w * vtemp.pack<3>("lambda", i) -
                                      kappa * w * cons;
          for (int d = 0; d != 3; ++d)
            if (cons[d] != 0) {
              vtemp("P", 4 * d, i) += kappa * w;
            }
        });
        // hess (embedded in multiply)
      }

      // prepare preconditioner
      cudaPol(
          zs::range(coVerts.size()), [vtemp = proxy<space>({}, vtemp), coOffset,
                                      kappa = kappa] __device__(int i) mutable {
            auto cons = vtemp.pack<3>("cons", i);
            auto w = vtemp("ws", coOffset + i);
            if (cons.l2NormSqr() != 0)
              vtemp.tuple<9>("P", coOffset + i) = mat3::identity() * kappa * w;
#if 0
                int d = 0;
                for (; d != 3 && cons[d] != 0; ++d)
                  ;
                for (; d != 3; ++d)
                  vtemp("P", 4 * d, coOffset + i) = kappa * w;
#endif
          });
      cudaPol(zs::range(numDofs),
              [vtemp = proxy<space>({}, vtemp)] __device__(int i) mutable {
                auto mat = vtemp.pack<3, 3>("P", i);
                if (zs::abs(zs::determinant(mat)) > limits<T>::epsilon() * 10)
                  vtemp.tuple<9>("P", i) = inverse(mat);
                else
                  vtemp.tuple<9>("P", i) = mat3::identity();
              });

      // modify initial x so that it satisfied the constraint.

      // A dir = grad
      if (useGD) {
        A.precondition(cudaPol, "grad", "dir");
        A.project(cudaPol, "dir");
      } else {
        // solve for A dir = grad;
        cudaPol(zs::range(numDofs),
                [vtemp = proxy<space>({}, vtemp)] __device__(int i) mutable {
                  vtemp.tuple<3>("dir", i) = vec3::zeros();
                });
        // temp = A * dir
        A.multiply(cudaPol, "dir", "temp");
        // r = grad - temp
        cudaPol(zs::range(numDofs),
                [vtemp = proxy<space>({}, vtemp)] __device__(int i) mutable {
                  vtemp.tuple<3>("r", i) =
                      vtemp.pack<3>("grad", i) - vtemp.pack<3>("temp", i);
                });
        A.project(cudaPol, "r");
        A.precondition(cudaPol, "r", "q");
        cudaPol(zs::range(numDofs),
                [vtemp = proxy<space>({}, vtemp)] __device__(int i) mutable {
                  vtemp.tuple<3>("p", i) = vtemp.pack<3>("q", i);
                });
        T zTrk = dot(cudaPol, vtemp, "r", "q");
        auto residualPreconditionedNorm = std::sqrt(zTrk);
        auto localTol = cgRel * residualPreconditionedNorm;
        int iter = 0;
        for (; iter != 10000; ++iter) {
          if (iter % 10 == 0)
            fmt::print("cg iter: {}, norm: {} (zTrk: {}) npp: {}, npe: {}, "
                       "npt: {}, nee: {}, ncspt: {}, ncsee: {}\n",
                       iter, residualPreconditionedNorm, zTrk, npp, npe, npt,
                       nee, ncspt, ncsee);
          if (zTrk < 0) {
            fmt::print(fg(fmt::color::pale_violet_red),
                       "what the heck? zTrk: {} at iteration {}. switching to "
                       "gradient descent ftm.\n",
                       zTrk, iter);
            useGD = true;
            // getchar();
            break;
          }
          if (residualPreconditionedNorm <= localTol)
            break;
          A.multiply(cudaPol, "p", "temp");
          A.project(cudaPol, "temp");

          T alpha = zTrk / dot(cudaPol, vtemp, "temp", "p");
          cudaPol(range(numDofs), [vtemp = proxy<space>({}, vtemp),
                                   alpha] ZS_LAMBDA(int vi) mutable {
            vtemp.tuple<3>("dir", vi) =
                vtemp.pack<3>("dir", vi) + alpha * vtemp.pack<3>("p", vi);
            vtemp.tuple<3>("r", vi) =
                vtemp.pack<3>("r", vi) - alpha * vtemp.pack<3>("temp", vi);
          });

          A.precondition(cudaPol, "r", "q");
          auto zTrkLast = zTrk;
          zTrk = dot(cudaPol, vtemp, "q", "r");
          auto beta = zTrk / zTrkLast;
          cudaPol(range(numDofs), [vtemp = proxy<space>({}, vtemp),
                                   beta] ZS_LAMBDA(int vi) mutable {
            vtemp.tuple<3>("p", vi) =
                vtemp.pack<3>("q", vi) + beta * vtemp.pack<3>("p", vi);
          });

          residualPreconditionedNorm = std::sqrt(zTrk);
        } // end cg step
        if (useGD == true)
          continue;
      }
      // recover rotated solution
      cudaPol(Collapse{vtemp.size()},
              [vtemp = proxy<space>({}, vtemp)] __device__(int vi) mutable {
                vtemp.tuple<3>("dir", vi) =
                    vtemp.pack<3, 3>("BCbasis", vi) * vtemp.pack<3>("dir", vi);
              });
      // check "dir" inf norm
      T res = infNorm(cudaPol, vtemp, "dir") / dt;
      T cons_res = A.constraintResidual(cudaPol);
      if (!useGD && res < targetGRes && cons_res == 0) {
        fmt::print("\t# newton optimizer ends in {} iters with residual {}\n",
                   newtonIter, res);
        break;
      }

      fmt::print(
          fg(fmt::color::aquamarine),
          "newton iter {}: direction residual(/dt) {}, grad residual {}\n",
          newtonIter, res, infNorm(cudaPol, vtemp, "grad"));

      // xn0 <- xn for line search
      cudaPol(zs::range(vtemp.size()),
              [vtemp = proxy<space>({}, vtemp)] __device__(int i) mutable {
                vtemp.tuple<3>("xn0", i) = vtemp.pack<3>("xn", i);
              });
      // initial energy
      T E0{};
      match([&](auto &elasticModel) {
        E0 = A.energy(cudaPol, elasticModel, "xn0", !BCsatisfied);
      })(models.getElasticModel());

      // line search
      bool CCDfiltered = false;
      T alpha = 1.;
      { //
#if 1
        // average length
        zs::Vector<T> lens{vtemp.get_allocator(), coOffset};
        cudaPol(Collapse{coOffset},
                [lens = proxy<space>(lens),
                 vtemp = proxy<space>({}, vtemp)] __device__(int ei) mutable {
                  // lens[ei] = vtemp.template pack<3>("dir", ei).norm();
                  lens[ei] = vtemp.template pack<3>("dir", ei).abs().sum();
                });
        auto meanDirSize = (reduce(cudaPol, lens) / dt) / coOffset;
        auto spanSize = meanDirSize * alpha / A.meanEdgeLength;
#else
        // infNorm
        auto spanSize = res * alpha / (A.meanEdgeLength * 8);
#endif
        if (spanSize > 1) { // mainly for reducing ccd pairs
          alpha /= spanSize;
          CCDfiltered = true;
          fmt::print("\tstepsize after dir magnitude pre-filtering: {} "
                     "(spansize: {})\n",
                     alpha, spanSize);
        }
      }
      A.groundIntersectionFreeStepsize(cudaPol, alpha);
      fmt::print("\tstepsize after ground: {}\n", alpha);
#if s_enableContact
      {
        A.intersectionFreeStepsize(cudaPol, xi, alpha);
        fmt::print("\tstepsize after intersection-free: {}\n", alpha);

        A.findCCDConstraints(cudaPol, alpha, xi);
        auto [npp, npe, npt, nee, ncspt, ncsee] = A.getCnts();
        A.intersectionFreeStepsize(cudaPol, xi, alpha);
        fmt::print("\tstepsize after ccd: {}. (ncspt: {}, ncsee: {})\n", alpha,
                   ncspt, ncsee);
/// check discrete collision
#if s_enableDCDCheck
        while (A.checkSelfIntersection(cudaPol)) {
          alpha /= 2;
          cudaPol(zs::range(vtemp.size()), [vtemp = proxy<space>({}, vtemp),
                                            alpha] __device__(int i) mutable {
            vtemp.tuple<3>("xn", i) =
                vtemp.pack<3>("xn0", i) + alpha * vtemp.pack<3>("dir", i);
          });
        }
#endif
      }
#endif

      T E{E0};
      T c1m = 0;
      int lsIter = 0;
      c1m = armijoParam * dot(cudaPol, vtemp, "dir", "grad");
      fmt::print(fg(fmt::color::white), "c1m : {}\n", c1m);
#if 1
      int numLineSearchIter = 0;
      do {
        cudaPol(zs::range(vtemp.size()), [vtemp = proxy<space>({}, vtemp),
                                          alpha] __device__(int i) mutable {
          vtemp.tuple<3>("xn", i) =
              vtemp.pack<3>("xn0", i) + alpha * vtemp.pack<3>("dir", i);
        });

        if constexpr (s_enableContact)
          A.findCollisionConstraints(cudaPol, dHat, xi);
        match([&](auto &elasticModel) {
          E = A.energy(cudaPol, elasticModel, "xn", !BCsatisfied);
        })(models.getElasticModel());

        fmt::print("E: {} at alpha {}. E0 {}\n", E, alpha, E0);
#if 0
        if (E < E0) break;
#else
        if (E <= E0 + alpha * c1m)
          break;
#endif

        alpha /= 2;
        if (++lsIter > 30) {
          auto cr = A.constraintResidual(cudaPol);
          fmt::print(
              "too small stepsize at iteration [{}]! alpha: {}, cons res: {}\n",
              lsIter, alpha, cr);
          if (!useGD && !CCDfiltered)
            getchar();
        }
      } while (true);
#endif

      cudaPol(zs::range(vtemp.size()), [vtemp = proxy<space>({}, vtemp),
                                        alpha] __device__(int i) mutable {
        vtemp.tuple<3>("xn", i) =
            vtemp.pack<3>("xn0", i) + alpha * vtemp.pack<3>("dir", i);
      });

/// check discrete collision
#if s_enableDCDCheck
      while (A.checkSelfIntersection(cudaPol)) {
        alpha /= 2;
        cudaPol(zs::range(vtemp.size()), [vtemp = proxy<space>({}, vtemp),
                                          alpha] __device__(int i) mutable {
          vtemp.tuple<3>("xn", i) =
              vtemp.pack<3>("xn0", i) + alpha * vtemp.pack<3>("dir", i);
        });
      }
#endif

      if (alpha < 1e-8) {
        useGD = true;
      } else {
        useGD = false;
      }

      // update rule
      cons_res = A.constraintResidual(cudaPol);
      if (res < updateZoneTol && cons_res > consTol) {
        if (kappa < kappaMax)
          kappa *= 2;
        else {
          cudaPol(Collapse{numDofs},
                  [vtemp = proxy<space>({}, vtemp),
                   kappa = kappa] __device__(int vi) mutable {
                    if (int BCorder = vtemp("BCorder", vi); BCorder > 0) {
                      vtemp.tuple<3>("lambda", vi) =
                          vtemp.pack<3>("lambda", vi) -
                          kappa * vtemp("ws", vi) * vtemp.pack<3>("cons", vi);
                    }
                  });
        }
      }
    } // end newton step

    // update velocity and positions
    A.updateVelocities(cudaPol);
#if 0
    // double -> float
    for (auto zstet : zstets) {
      if (!zstet->hasImage(ZenoParticles::s_particleTag)) {
        auto &loVerts = zstet->getParticles();
        auto &verts = zstet->getParticles<true>();
        cudaPol(range(loVerts.size()),
                [loVerts = proxy<space>({}, loVerts),
                 verts = proxy<space>({}, verts)] __device__(int vi) mutable {
                  loVerts.tuple<3>("x", vi) = verts.pack<3>("x", vi);
                  loVerts.tuple<3>("v", vi) = verts.pack<3>("v", vi);
                });
      }
    }
#endif
    // not sure if this is necessary for numerical reasons
    if (coVerts.size())
      cudaPol(zs::range(coVerts.size()),
              [vtemp = proxy<space>({}, vtemp),
               verts = proxy<space>({}, zsboundary->getParticles<true>()),
               loVerts = proxy<space>({}, zsboundary->getParticles()),
               coOffset] __device__(int vi) mutable {
                auto newX = vtemp.pack<3>("xn", coOffset + vi);
                verts.tuple<3>("x", vi) = newX;
                loVerts.tuple<3>("x", vi) = newX;
                // no need to update v here. positions are moved accordingly
                // also, boundary velocies are set elsewhere
              });

    set_output("ZSParticles", get_input("ZSParticles"));
  }
};

ZENDEFNODE(CodimStepping, {{
                               "ZSParticles",
                               "ZSBoundaryPrimitives",
                               {"int", "est_num_cps", "0"},
                               {"float", "dt", "0.01"},
                               {"float", "dHat", "0.005"},
                               {"float", "kappa0", "1e3"},
                               {"float", "aug_coeff", "1e3"},
                               {"float", "pn_rel", "0.01"},
                               {"float", "cg_rel", "0.01"},
                               {"float", "gravity", "-9."},
                           },
                           {"ZSParticles"},
                           {},
                           {"FEM"}});

} // namespace zeno

#include "Ipc.inl"