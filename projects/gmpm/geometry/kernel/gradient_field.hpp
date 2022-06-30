#pragma once

#include "../../Structures.hpp"

namespace zeno{
    template<typename T,typename Pol,int dim = 3>
    constexpr void compute_gradient(Pol &pol,const typename ZenoParticles::particles_t &eles,
        const typename ZenoParticles::particles_t &verts,const zs::SmallString& xTag,
        const zs::TileVector<T,32>& vtemp,const zs::SmallString& tTag,
        zs::TileVector<T,32>& etemp,const zs::SmallString& gTag,zs::wrapv<dim> = {}) {
            using namespace zs;
            using mat = zs::vec<T,dim,dim>;
            using vec = zs::vec<T,dim>;

            static_assert(dim == 3 || dim == 2,"invalid dimension!\n");
            constexpr auto dimp1 = dim + 1;

            constexpr auto space = Pol::exec_tag::value;
            #if ZS_ENABLE_CUDA && defined(__CUDACC__)
                static_assert(space == execspace_e::cuda,"specified policy and compiler not match");
            #else
                static_assert(space != execspace_e::cuda,"specified policy and compiler not match");
            #endif
            if(!verts.hasProperty(xTag)){
                // printf("verts buffer does not contain specified channel\n");
                throw std::runtime_error("verts buffer does not contain specified channel");
            }
            if(!vtemp.hasProperty(tTag)){
                throw std::runtime_error("vtemp buffer does not contain specified channel");
            }
            if(!etemp.hasProperty(gTag)){
                // printf("etemp buffer does not contain specified channel\n");
                throw std::runtime_error("etemp buffer does not contain specified channel");
            }
            if(eles.getChannelSize("inds") != dimp1){
                throw std::runtime_error("the specified dimension does not match the input simplex size");
            }
            // etemp.append_channels(pol,{{gTag,dim}});

            pol(range(eles.size()),
                [eles = proxy<space>({},eles),verts = proxy<space>({},verts),etemp = proxy<space>({},etemp),
                    vtemp = proxy<space>({},vtemp),xTag,tTag,gTag]
                    ZS_LAMBDA(int ei) mutable {
                        // constexpr int dim = RM_CVREF_T(dim_v)::value;
                        constexpr int dimp1 = dim + 1;
                        mat Dm = mat{(T)0.0};
                        auto inds = eles.template pack<dimp1>("inds",ei).template reinterpret_bits<int>();
                        vec g{};
                        for(size_t i = 0;i != dim;++i){
                            // zs::row(Dm,i) = verts.pack<dim>(xTag,inds[i+1]) - verts.pack<dim>(xTag,inds[0]);
                            zs::tie(Dm(i, 0), Dm(i, 1), Dm(i, 2)) = verts.pack<dim>(xTag,inds[i+1]) - verts.pack<dim>(xTag,inds[0]);
                            g[i] = vtemp(tTag,inds[i+1]) - vtemp(tTag,inds[0]);
                        }
                        // if(ei == 0){
                        //     printf("verts[0] : %f %f %f\n",(float)verts.pack<dim>(xTag,inds[0])[0],(float)verts.pack<dim>(xTag,inds[0])[1],(float)verts.pack<dim>(xTag,inds[0])[2]);
                        //     printf("verts[1] : %f %f %f\n",(float)verts.pack<dim>(xTag,inds[1])[0],(float)verts.pack<dim>(xTag,inds[1])[1],(float)verts.pack<dim>(xTag,inds[1])[2]);
                        //     printf("verts[2] : %f %f %f\n",(float)verts.pack<dim>(xTag,inds[2])[0],(float)verts.pack<dim>(xTag,inds[2])[1],(float)verts.pack<dim>(xTag,inds[2])[2]);
                        //     printf("verts[3] : %f %f %f\n",(float)verts.pack<dim>(xTag,inds[3])[0],(float)verts.pack<dim>(xTag,inds[3])[1],(float)verts.pack<dim>(xTag,inds[3])[2]);
                        //     printf("Dm[0] : \n%f %f %f\n%f %f %f\n%f %f %f\n",
                        //         (float)Dm(0,0),(float)Dm(0,1),(float)Dm(0,2),
                        //         (float)Dm(1,0),(float)Dm(1,1),(float)Dm(1,2),
                        //         (float)Dm(2,0),(float)Dm(2,1),(float)Dm(2,2));
                        //     printf("g[0] : %f %f %f\n",(float)g[0],(float)g[1],(float)g[2]);
                        // }

                        Dm = zs::inverse(Dm);
                        etemp.template tuple<dim>(gTag,ei) = Dm * g;


            });
    }
};