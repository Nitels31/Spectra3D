#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include <cstdio>

int main(int argc, char** argv) {
    cgltf_options opts = {};
    cgltf_data* d = nullptr;
    if (cgltf_parse_file(&opts, argv[1], &d) != cgltf_result_success) {
        printf("parse fail\n");
        return 1;
    }
    if (cgltf_load_buffers(&opts, d, argv[1]) != cgltf_result_success) {
        printf("buffers fail\n");
        return 1;
    }
    printf("meshes=%zu materials=%zu nodes=%zu scenes=%zu\n",
           d->meshes_count, d->materials_count, d->nodes_count, d->scenes_count);
    for (size_t m = 0; m < d->meshes_count; ++m) {
        printf("mesh[%zu] '%s' prims=%zu\n", m,
               d->meshes[m].name ? d->meshes[m].name : "?",
               d->meshes[m].primitives_count);
        for (size_t p = 0; p < d->meshes[m].primitives_count; ++p) {
            cgltf_primitive& pr = d->meshes[m].primitives[p];
            printf("  prim[%zu]: attrs=%zu indices=%s mat=%d\n", p,
                   pr.attributes_count, pr.indices ? "Y" : "N",
                   pr.material ? (int)(pr.material - d->materials) : -1);
            for (size_t a = 0; a < pr.attributes_count; ++a) {
                cgltf_accessor* acc = pr.attributes[a].data;
                printf("    attr type=%d count=%zu", (int)pr.attributes[a].type,
                       (size_t)acc->count);
                if (acc->has_min && acc->min)
                    printf(" min=(%.2f %.2f %.2f)", acc->min[0], acc->min[1],
                           acc->min[2]);
                if (acc->has_max && acc->max)
                    printf(" max=(%.2f %.2f %.2f)", acc->max[0], acc->max[1],
                           acc->max[2]);
                printf("\n");
            }
            if (pr.material) {
                printf("    color = %.2f %.2f %.2f %.2f\n",
                       pr.material->pbr_metallic_roughness.base_color_factor[0],
                       pr.material->pbr_metallic_roughness.base_color_factor[1],
                       pr.material->pbr_metallic_roughness.base_color_factor[2],
                       pr.material->pbr_metallic_roughness.base_color_factor[3]);
            }
        }
    }
    for (size_t n = 0; n < d->nodes_count; ++n) {
        cgltf_node* nd = &d->nodes[n];
        printf("node[%zu] '%s' mesh=%s children=%zu hasTRS=%d\n", n,
               nd->name ? nd->name : "?", nd->mesh ? "Y" : "N",
               nd->children_count, (int)nd->has_translation);
    }
    cgltf_free(d);
    return 0;
}
