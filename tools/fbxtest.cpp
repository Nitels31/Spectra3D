#include "ufbx.h"
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "models/cube.fbx";
    ufbx_load_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.generate_missing_normals = true;
    opts.target_unit_meters = 1.0f;
    opts.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;
    ufbx_error err;
    ufbx_scene* sc = ufbx_load_file(path, &opts, &err);
    if (!sc) {
        printf("LOAD FAILED: type=%d\n", (int)err.type);
        if (err.description.data)
            printf("desc: %.*s\n", (int)err.description.length, err.description.data);
        return 1;
    }
    printf("loaded: nodes=%zu meshes=%zu materials=%zu\n",
           sc->nodes.count, sc->meshes.count, sc->materials.count);
    for (size_t i = 0; i < sc->nodes.count; ++i) {
        ufbx_node* n = sc->nodes.data[i];
        printf("node[%zu] '%.*s' mesh=%s children=%zu\n", i,
               (int)n->name.length, n->name.data, n->mesh ? "YES" : "no",
               n->children.count);
        const ufbx_matrix* m = &n->node_to_world;
        printf("  node_to_world: %g %g %g %g | %g %g %g %g | %g %g %g %g\n",
               m->m00, m->m01, m->m02, m->m03, m->m10, m->m11, m->m12, m->m13,
               m->m20, m->m21, m->m22, m->m23);
        const ufbx_matrix* g = &n->geometry_to_world;
        printf("  geom_to_world: %g %g %g %g | %g %g %g %g | %g %g %g %g\n",
               g->m00, g->m01, g->m02, g->m03, g->m10, g->m11, g->m12, g->m13,
               g->m20, g->m21, g->m22, g->m23);
        if (!n->mesh) continue;
        ufbx_mesh* mesh = n->mesh;
        printf("  mesh: verts=%zu indices=%zu faces=%zu normals=%zu/%zu\n",
               mesh->num_vertices, mesh->num_indices, mesh->num_faces,
               mesh->vertex_normal.values.count, mesh->vertex_normal.indices.count);
        if (mesh->num_vertices > 0) {
            ufbx_vec3 v = mesh->vertices.data[0];
            printf("  v0 = %g %g %g\n", v.x, v.y, v.z);
        }
        if (mesh->num_faces > 0) {
            ufbx_face f = mesh->faces.data[0];
                printf("  face0: begin=%zu num=%zu\n", (size_t)f.index_begin,
                   (size_t)f.num_indices);
            if (f.num_indices > 0) {
                ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position,
                                                   f.index_begin);
                printf("  corner0 pos = %g %g %g\n", p.x, p.y, p.z);
                if (mesh->vertex_normal.values.data) {
                    ufbx_vec3 nn = ufbx_get_vertex_vec3(&mesh->vertex_normal,
                                                        f.index_begin);
                    printf("  corner0 nrm = %g %g %g\n", nn.x, nn.y, nn.z);
                } else {
                    printf("  corner0 nrm = MISSING\n");
                }
            }
        }
    }
    ufbx_free_scene(sc);
    return 0;
}
