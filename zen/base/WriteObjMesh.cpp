#include <zen/zen.h>
#include <zen/MeshObject.h>
#include <zen/StringObject.h>
#include <cstring>

namespace zenbase {

static void writeobj(
    const char *path,
    std::vector<glm::vec3> &face_vertices,
    std::vector<glm::vec2> &face_uvs,
    std::vector<glm::vec3> &face_normals)
{
  FILE *fp = fopen(path, "w");
  if (!fp) {
    perror(path);
    abort();
  }

  for (auto const &v: face_vertices) {
    fprintf(fp, "v %f %f %f\n", v.x, v.y, v.z);
  }
  for (auto const &v: face_uvs) {
    fprintf(fp, "vt %f %f\n", v.x, v.y);
  }
  for (auto const &v: face_normals) {
    fprintf(fp, "vn %f %f %f\n", v.x, v.y, v.z);
  }

  for (int i = 0; i < face_vertices.size(); i += 3) {
    int a = i + 1, b = i + 2, c = i + 3;
    fprintf(fp, "f %d/%d/%d %d/%d/%d %d/%d/%d\n",
        a, a, a, b, b, b, c, c, c);
  }
  fclose(fp);
}


struct WriteObjMesh : zen::INode {
  virtual void apply() override {
    auto path = std::get<std::string>(get_param("path"));
    auto mesh = get_input("mesh")->as<MeshObject>();
    writeobj(path.c_str(), mesh->vertices, mesh->uvs, mesh->normals);
  }
};

static int defWriteObjMesh = zen::defNodeClass<WriteObjMesh>("WriteObjMesh",
    { /* inputs: */ {
    "mesh",
    }, /* outputs: */ {
    }, /* params: */ {
    {"string", "path", ""},
    }, /* category: */ {
    "trimesh",
    }});


struct ExportObjMesh : zen::INode {
  virtual void apply() override {
    auto path = get_input("path")->as<StringObject>();
    auto mesh = get_input("mesh")->as<MeshObject>();
    writeobj(path->get().c_str(), mesh->vertices, mesh->uvs, mesh->normals);
  }
};

static int defExportObjMesh = zen::defNodeClass<ExportObjMesh>("ExportObjMesh",
    { /* inputs: */ {
    "mesh",
    }, /* outputs: */ {
    "path",
    }, /* params: */ {
    }, /* category: */ {
    "trimesh",
    }});

}
