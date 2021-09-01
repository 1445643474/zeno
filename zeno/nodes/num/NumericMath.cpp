#include <zeno/zeno.h>
#include <zeno/types/NumericObject.h>
#include <zeno/utils/random.h>

namespace {

using namespace zeno;

struct MakeOrthonormalBase : INode {
    virtual void apply() override {
        auto normal = get_input<NumericObject>("normal")->get<vec3f>();
        normal = normalize(normal);
        vec3f tangent, bitangent;
        if (has_input("tangent")) {
            tangent = get_input<NumericObject>("tangent")->get<vec3f>();
            bitangent = cross(normal, tangent);
        } else {
            tangent = vec3f(0, 0, 1);
            bitangent = cross(normal, tangent);
            if (dot(bitangent, bitangent) < 1e-5) {
                tangent = vec3f(0, 1, 0);
               bitangent = cross(normal, tangent);
            }
        }
        bitangent = normalize(bitangent);
        tangent = cross(bitangent, normal);

        set_output("normal", std::make_shared<NumericObject>(normal));
        set_output("tangent", std::make_shared<NumericObject>(tangent));
        set_output("bitangent", std::make_shared<NumericObject>(bitangent));
    }
};

ZENDEFNODE(MakeOrthonormalBase, {
    {{"vec3f", "normal", "0,0,1"}, {"vec3f", "tangent", "0,1,0"}},
    {{"vec3f", "normal"}, {"vec3f", "tangent"}, {"vec3f", "bitangent"}},
    {},
    {"math"},
});


struct BoundingBoxCollideDetect : INode {
    virtual void apply() override {
        auto bminA = get_input<NumericObject>("bminA")->get<vec3f>();
        auto bmaxA = get_input<NumericObject>("bmaxA")->get<vec3f>();
        auto bminB = get_input<NumericObject>("bminB")->get<vec3f>();
        auto bmaxB = get_input<NumericObject>("bmaxB")->get<vec3f>();

        // https://www.cnblogs.com/liez/p/11965027.html
        bool overlap = alltrue(abs(bminA + bmaxA - bminB - bmaxB) <= (bmaxA - bminA - bmaxA + bminB));
        set_output2("overlap", overlap);
        bool AinsideB = alltrue(bminA >= bminB && bmaxA <= bmaxB);
        set_output2("AinsideB", AinsideB);
        bool BinsideA = alltrue(bminA <= bminB && bmaxA >= bmaxB);
        set_output2("BinsideA", BinsideA);
    }
};

ZENDEFNODE(BoundingBoxCollideDetect, {
    {{"vec3f", "bminA"}, {"vec3f", "bmaxA"}, {"vec3f", "bminB"}, {"vec3f", "bmaxB"}},
    {{"bool", "overlap"}, {"bool", "AinsideB"}, {"bool", "BinsideA"}},
    {},
    {"math"},
});


struct UnpackNumericVec : INode {
    virtual void apply() override {
        auto vec = get_input<NumericObject>("vec")->value;
        NumericValue x = 0, y = 0, z = 0, w = 0;
        std::visit([&x, &y, &z, &w] (auto const &vec) {
            using T = std::decay_t<decltype(vec)>;
            if constexpr (!is_vec_v<T>) {
                x = vec;
            } else {
                if constexpr (is_vec_n<T> > 0) x = vec[0];
                if constexpr (is_vec_n<T> > 1) y = vec[1];
                if constexpr (is_vec_n<T> > 2) z = vec[2];
                if constexpr (is_vec_n<T> > 3) w = vec[3];
            }
        }, vec);
        set_output("X", std::make_shared<NumericObject>(x));
        set_output("Y", std::make_shared<NumericObject>(y));
        set_output("Z", std::make_shared<NumericObject>(z));
        set_output("W", std::make_shared<NumericObject>(w));
    }
};

ZENDEFNODE(UnpackNumericVec, {
    {{"vec3f", "vec"}},
    {{"float", "X"}, {"float", "Y"},
     {"float", "Z"}, {"float", "W"}},
    {},
    {"numeric"},
}); // TODO: add PackNumericVec too.


struct NumericRandom : INode {
    virtual void apply() override {
        auto value = std::make_shared<NumericObject>();
        auto dim = get_param<int>("dim");
        if (dim == 1) {
            value->set(float(frand()));
        } else if (dim == 2) {
            value->set(zeno::vec2f(frand(), frand()));
        } else if (dim == 3) {
            value->set(zeno::vec3f(frand(), frand(), frand()));
        } else if (dim == 4) {
            value->set(zeno::vec4f(frand(), frand(), frand(), frand()));
        } else {
            printf("invalid dim for NumericRandom: %d\n", dim);
        }
        set_output("value", std::move(value));
    }
};

ZENDEFNODE(NumericRandom, {
    {},
    {{"NumericObject", "value"}},
    {{"int", "dim", "1"}},
    {"numeric"},
});


struct SetRandomSeed : INode {
    virtual void apply() override {
        auto seed = get_input<NumericObject>("seed")->get<int>();
        sfrand(seed);
        if (has_input2("routeIn")) {
            set_output2("routeOut", get_input2("routeIn"));
        } else {
            set_output2("routeOut", std::make_shared<NumericObject>(seed));
        }
    }
};

ZENDEFNODE(SetRandomSeed, {
    {"routeIn", {"int", "seed", "0"}},
    {"routeOut"},
    {},
    {"numeric"},
});


struct NumericCounter : INode {
    int counter = 0;

    virtual void apply() override {
        auto count = std::make_shared<NumericObject>();
        count->value = counter++;
        set_output("count", std::move(count));
    }
};

ZENDEFNODE(NumericCounter, {
    {},
    {{"int", "count"}},
    {},
    {"numeric"},
});


}
