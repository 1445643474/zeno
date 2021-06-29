#include <zeno/zen.h>
#include <zeno/MeshObject.h>
#include <zeno/NumericObject.h>
#include <zeno/VDBGrid.h>
#include <omp.h>
#include "FLIP_vdb.h"


/*void FLIP_vdb::field_add_vector(openvdb::FloatGrid::Ptr velocity_field,
	openvdb::FloatGrid::Ptr face_weight,
	float x, float y, float z, float dt)
*/

namespace zen{
    
    struct FieldAddVector : zen::INode{
        virtual void apply() override {
            // auto dt = get_input("dt")->as<zen::NumericObject>()->get<float>();
            // float vx = std::get<float>(get_param("vx"));
            // float vy = std::get<float>(get_param("vy"));
            // float vz = std::get<float>(get_param("vz"));
            auto ivec3 = get_input("invec3")->as<zen::NumericObject>()->get<zen::vec3f>();
            auto velocity = get_input("Velocity")->as<VDBFloat3Grid>();
            if(has_input("FieldWeight")) {
                auto face_weight = get_input("FieldWeight")->as<VDBFloat3Grid>();
            
                FLIP_vdb::field_add_vector(velocity->m_grid, 
                face_weight->m_grid, ivec3[0],ivec3[1],ivec3[2], 1.0);
            }
            else {
                using TmpT = decltype(std::declval<VDBFloat3Grid>().m_grid);
                TmpT tmp{nullptr};
                FLIP_vdb::field_add_vector(velocity->m_grid, 
                tmp, ivec3[0],ivec3[1],ivec3[2], 1.0);
            }
        }
    };

static int defFieldAddVector = zen::defNodeClass<FieldAddVector>("FieldAddVector",
    { /* inputs: */ {
        "invec3", "Velocity", "FieldWeight", 
    }, 
    /* outputs: */ {
    }, 
    /* params: */ {
       
    }, 
    
    /* category: */ {
    "FLIPSolver",
    }});

}
