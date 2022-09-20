#pragma once

#include "Structures.hpp"
#include "Utils.hpp"
#include "zensim/cuda/execution/ExecutionPolicy.cuh"
#include "zensim/omp/execution/ExecutionPolicy.hpp"

#include "kernel/geo_math.hpp"

namespace zeno {

struct ZSCalcSurfaceTenssionField : INode {
    using dtiles_t = zs::TileVector<float,32>;

    virtual void apply() override {
        using namespace zs;
        auto zssurf = get_input<ZenoParticles>("zssurf");
        auto ref_channel = get_param<std::string>("ref_channel");
        auto def_channel = get_param<std::string>("def_channel");
        auto tension_tag = get_param<std::string>("tension_channel");

        auto& verts = zssurf->getParticles();
        auto& tris = zssurf->getQuadraturePoints();

        if(tris.getChannelSize("inds") != 3) {
            fmt::print("ZSCalcSurfaceTenssionField only supports triangle surface mesh");
            throw std::runtime_error("ZSCalcSurfaceTenssionField only supports triangle surface mesh");
        }
        if(!verts.hasProperty(ref_channel)){
            fmt::print("the input surf does not contain {} channel\n",ref_channel);
            throw std::runtime_error("the input surf does not contain specified referenced channel\n");
        }
        if(!verts.hasProperty(def_channel)) {
            fmt::print("the input surf does not contain {} channel\n",def_channel);
            throw std::runtime_error("the input surf does not contain specified deformed channel\n");
        }

        // if(!verts.hasProperty("nm_incident_facets")) {
        //     // compute the number of incident facets
        //     verts.append_channels({{"nm_incident_facets",1}});
        //     cudaExec(zs::range(verts.size()),
        //         [verts = proxy<space>({},verts)] ZS_LAMBDA(int vi) {
        //             verts("nm_incident_facets",vi) = 0;
        //     });

        //     cudaExec(zs::range(tris.size() * 3),
        //         [verts = proxy<space>({},verts),tris = proxy<space>({},tris)]
        //             ZS_LAMBDA(int ti_dof) mutable {

        //     });
        // }

        // by default we write the facet tension over per facet's specified channel
        auto cudaExec = zs::cuda_exec();
        const auto nmVerts = verts.size();
        const auto nmTris = tris.size();

        if(!verts.hasProperty(tension_tag))
            verts.append_channels(cudaExec,{{tension_tag,1}});
        if(!tris.hasProperty(tension_tag))
            tris.append_channels(cudaExec,{{tension_tag,1}});

        dtiles_t vtemp{verts.get_allocator(),
            {
                {"a",1},
                {"A",1}
            },
        verts.size()};

        dtiles_t etemp{tris.get_allocator(),
            {
                {"A",1},
                {"a",1}
            },
        tris.size()};

        vtemp.resize(verts.size());
        etemp.resize(tris.size());

        constexpr auto space = zs::execspace_e::cuda;



        cudaExec(zs::range(nmTris),
            [tris = proxy<space>({},tris),verts = proxy<space>({},verts),
                    ref_channel = zs::SmallString(ref_channel),
                    def_channel = zs::SmallString(def_channel),
                    etemp = proxy<space>({},etemp)] ZS_LAMBDA (int ti) mutable {
                const auto& inds = tris.template pack<3>("inds",ti).reinterpret_bits<int>(); 
                const auto& X0 = verts.template pack<3>(ref_channel,inds[0]);
                const auto& X1 = verts.template pack<3>(ref_channel,inds[1]);
                const auto& X2 = verts.template pack<3>(ref_channel,inds[2]);
                const auto& x0 = verts.template pack<3>(def_channel,inds[0]);
                const auto& x1 = verts.template pack<3>(def_channel,inds[1]);
                const auto& x2 = verts.template pack<3>(def_channel,inds[2]);
                
                auto A = LSL_GEO::area<float>(X0,X1,X2);
                auto a = LSL_GEO::area<float>(x0,x1,x2);

                etemp("A",ti) = A;
                etemp("a",ti) = a;
                // etemp("ratio",ti) = a / (A + 1e-8);
        });

        cudaExec(zs::range(nmVerts),
            [vtemp = proxy<space>({},vtemp)] ZS_LAMBDA(int vi) mutable {
                vtemp("a",vi) = 0;
                vtemp("A",vi) = 0;
        });

        cudaExec(zs::range(nmTris * 3),
            [tris = proxy<space>({},tris),vtemp = proxy<space>({},vtemp),
                etemp = proxy<space>({},etemp),execTag = wrapv<space>{}] ZS_LAMBDA(int vid) mutable {
            int ti = vid / 3;
            const auto& inds = tris.template pack<3>("inds",ti).reinterpret_bits<int>();
            auto A = etemp("A",ti);
            auto a = etemp("a",ti);
            atomic_add(execTag,&vtemp("A",inds[vid % 3]),A/3);
            atomic_add(execTag,&vtemp("a",inds[vid % 3]),a/3);
        });

        // cudaExec(zs::range(nmVerts),
        //         [vtemp = proxy<space>({},etemp)] ZS_LAMBDA(int tid) mutable {
        //     etemp("A",tid)
        // });

        // blend the tension nodal-wise
        cudaExec(zs::range(nmVerts),
            [verts = proxy<space>({},verts),tension_tag = zs::SmallString(tension_tag),
                    vtemp = proxy<space>({},vtemp)] ZS_LAMBDA (int vi) mutable {
                verts(tension_tag,vi) = vtemp("a",vi) / (vtemp("A",vi));
        });
        cudaExec(zs::range(nmTris),
            [tris = proxy<space>({},tris),tension_tag = zs::SmallString(tension_tag),
                    etemp = proxy<space>({},etemp)] ZS_LAMBDA (int vi) mutable {
                tris(tension_tag,vi) = etemp("a",vi) / (etemp("A",vi));
        });        

        set_output("zssurf",zssurf);
    }
};

ZENDEFNODE(ZSCalcSurfaceTenssionField, {
                            {"zssurf"},
                            {"zssurf"},
                            {{"string","ref_channel","X"},{"string","def_channel","x"},{"string","tension_channel"," tension"}},
                            {"ZSGeometry"}});

};