/* Goxel 3D voxels editor
 *
 * copyright (c) 2015 Guillaume Chereau <guillaume@noctua-software.com>
 *
 * Goxel is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.

 * Goxel is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.

 * You should have received a copy of the GNU General Public License along with
 * goxel.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "goxel.h"

/*
 * Here is the convention I used for the cube vertices, edges and faces:
 *
 *           v7 +----------e6---------+ v6
 *             /.                    /|
 *            / .                   / |
 *         e11  .                e10  |                    +-----------+
 *          /   .                 /   |                   /           /|
 *         /    .                /    |                  /   f3      / |  <f1
 *     v3 +----------e2---------+ v2  |                 +-----------+  |
 *        |     .               |     e5            f5> |           |f4|
 *        |     e7              |     |                 |           |  |
 *        |     .               |     |                 |    f0     |  +
 *        |     .               |     |                 |           | /
 *        |  v4 . . . .e4 . . . | . . + v5              |           |/
 *       e3    .                |    /                  +-----------+
 *        |   .                e1   /                         ^
 *        |  e8                 |  e9                         f2
 *        | .                   | /
 *        |.                    |/
 *     v0 +---------e0----------+ v1
 *
 */

static const int N = BLOCK_SIZE;

#define BLOCK_ITER(x, y, z) \
    for (z = 0; z < N; z++) \
        for (y = 0; y < N; y++) \
            for (x = 0; x < N; x++)

#define BLOCK_ITER_INSIDE(x, y, z) \
    for (z = 1; z < N - 1; z++) \
        for (y = 1; y < N - 1; y++) \
            for (x = 1; x < N - 1; x++)

#define DATA_AT(d, x, y, z) (d->voxels[x + y * N + z * N * N])
#define BLOCK_AT(c, x, y, z) (DATA_AT(c->data, x, y, z))

// face index -> [vertex0, vertex1, vertex2, vertex3]
const int FACES_VERTICES[6][4] = {
    {0, 1, 2, 3},
    {5, 4, 7, 6},
    {0, 4, 5, 1},
    {2, 6, 7, 3},
    {1, 5, 6, 2},
    {0, 3, 7, 4}
};

// face index + edge -> neighbor face index.
const int FACES_NEIGHBORS[6][4] = {
    {2, 4, 3, 5},
    {2, 5, 3, 4},
    {5, 1, 4, 0},
    {4, 1, 5, 0},
    {2, 1, 3, 0},
    {0, 3, 1, 2},
};

// vertex index -> vertex position
const vec3b_t VERTICES_POSITIONS[8] = {
    IVEC(0, 0, 1),
    IVEC(1, 0, 1),
    IVEC(1, 1, 1),
    IVEC(0, 1, 1),
    IVEC(0, 0, 0),
    IVEC(1, 0, 0),
    IVEC(1, 1, 0),
    IVEC(0, 1, 0)
};

const uvec2b_t VERTICE_UV[4] = {
    IVEC(0, 0),
    IVEC(1, 0),
    IVEC(1, 1),
    IVEC(0, 1),
};

const vec3b_t FACES_NORMALS[6] = {
    IVEC( 0,  0,  1),
    IVEC( 0,  0, -1),
    IVEC( 0, -1,  0),
    IVEC( 0,  1,  0),
    IVEC( 1,  0,  0),
    IVEC(-1,  0,  0),
};

// XXX: Keep orientation.
const vec4b_t FACES_ROTATIONS[6] = {
    IVEC( 0,  1,  0,  0),
    IVEC( 2,  1,  0,  0),
    IVEC( 1,  1,  0,  0),
    IVEC(-1,  1,  0,  0),
    IVEC( 1,  0,  1,  0),
    IVEC(-1,  0,  1,  0),
};

const int EDGES_VERTICES[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7},
};

static int make_id(void)
{
    return ++goxel()->block_next_id;
}

static block_data_t *get_empty_data(void)
{
    static block_data_t *data = NULL;
    if (!data) {
        data = calloc(1, sizeof(*data));
        data->ref = 1;
        data->id = 0;
        goxel()->block_count++;
    }
    return data;
}

bool block_is_empty(const block_t *block, bool fast)
{
    int x, y, z;
    if (!block) return true;
    if (block->data->id == 0) return true;
    if (fast) return false;

    BLOCK_ITER(x, y, z) {
        if (BLOCK_AT(block, x, y, z).a) return false;
    }
    return true;
}

block_t *block_new(const vec3_t *pos, block_data_t *data)
{
    block_t *block = calloc(1, sizeof(*block));
    block->pos = *pos;
    block->data = data ?: get_empty_data();
    block->data->ref++;
    return block;
}

void block_delete(block_t *block)
{
    block->data->ref--;
    if (block->data->ref == 0) {
        free(block->data);
        goxel()->block_count--;
    }
    free(block);
}

block_t *block_copy(const block_t *other)
{
    block_t *block = malloc(sizeof(*block));
    *block = *other;
    block->next = block->prev = NULL;
    block->data->ref++;
    return block;
}

void block_set_data(block_t *block, block_data_t *data)
{
    block->data->ref--;
    if (block->data->ref == 0) {
        free(block->data);
        goxel()->block_count--;
    }
    block->data = data;
    data->ref++;
}

box_t block_get_box(const block_t *block, bool exact)
{
    box_t ret;
    int x, y, z;
    int xmin = N, xmax = 0, ymin = N, ymax = 0, zmin = N, zmax = 0;
    if (!exact)
        return bbox_from_extents(block->pos, N / 2, N / 2, N / 2);
    BLOCK_ITER(x, y, z) {
        if (BLOCK_AT(block, x, y, z).a) {
            xmin = min(xmin, x);
            ymin = min(ymin, y);
            zmin = min(zmin, z);
            xmax = max(xmax, x);
            ymax = max(ymax, y);
            zmax = max(zmax, z);
        }
    }
    if (xmin > xmax) return box_null();
    ret = bbox_from_points(vec3(xmin - 0.5, ymin - 0.5, zmin - 0.5),
                           vec3(xmax + 0.5, ymax + 0.5, zmax + 0.5));
    vec3_iadd(&ret.p, block->pos);
    vec3_isub(&ret.p, vec3(N / 2 - 0.5, N / 2 - 0.5, N / 2 - 0.5));
    return ret;
}

static uint32_t block_get_neighboors(const block_data_t *data,
                                    int x, int y, int z,
                                    uint8_t neighboors[27])
{
    int xx, yy, zz, i = 0;
    vec3b_t npos;
    uint32_t ret = 0;
#define ITER_NEIGHBORS(x, y, z)         \
     for (z = -1; z <= 1; z++)           \
         for (y = -1; y <= 1; y++)       \
             for (x = -1; x <= 1; x++)
    ITER_NEIGHBORS(xx, yy, zz) {
        npos = vec3b(x + xx, y + yy, z + zz);
        neighboors[i] = DATA_AT(data, npos.x, npos.y, npos.z).a;
        if (neighboors[i] >= 127) ret |= 1 << i;
        i++;
    }
#undef ITER_NEIGHBORS
    return ret;
}

static bool block_is_face_visible(uint32_t neighboors_mask, int f)
{
#define M(x, y, z) (1 << ((x + 1) + (y + 1) * 3 + (z + 1) * 9))
    static const uint32_t MASKS[6] = {
        M(0, 0, 1), M(0, 0, -1), M(0, -1, 0),
        M(0, 1, 0), M(1, 0, 0), M(-1, 0, 0),
    };
#undef M
    return !(MASKS[f] & neighboors_mask);
}

static vec3b_t block_get_normal(uint32_t neighboors_mask,
                                const uint8_t neighboors[27], int f,
                                float smooth)
{
    int x, y, z, i = 0;
    int sx = 0, sy = 0, sz = 0, ssum = 0;
    if (!smooth) return FACES_NORMALS[f];
    for (z = -1; z <= +1; z++)
    for (y = -1; y <= +1; y++)
    for (x = -1; x <= +1; x++) {
        if (neighboors_mask & (1 << i)) {
            ssum += neighboors[i];
            sx -= neighboors[i] * x;
            sy -= neighboors[i] * y;
            sz -= neighboors[i] * z;
        }
        i++;
    }
    if (sx == 0 && sy == 0 && sz == 0)
        return FACES_NORMALS[f];
    return vec3b(sx * 127 / ssum, sy * 127 / ssum, sz * 127 / ssum);
}

static bool block_get_edge_border(uint32_t neighboors_mask, int f, int e)
{
#define M(x, y, z) (1 << ((x + 1) + (y + 1) * 3 + (z + 1) * 9))
    static const uint32_t MASKS[6][4] = {
        /* F0 */ {M(0, -1, 1), M(1, 0, 1), M(0, 1, 1), M(-1, 0, 1)},
        /* F1 */ {M(0, -1, -1), M(-1, 0, -1), M(0, 1, -1), M(1, 0, -1)},
        /* F2 */ {M(-1, -1, 0), M(0, -1, -1), M(1, -1, 0), M(0, -1, 1)},
        /* F3 */ {M(1, 1, 0), M(0, 1, -1), M(-1, 1, 0), M(0, 1, 1)},
        /* F4 */ {M(1, -1, 0), M(1, 0, -1), M(1, 1, 0), M(1, 0, 1)},
        /* F5 */ {M(-1, 0, 1), M(-1, 1, 0), M(-1, 0, -1), M(-1, -1, 0)},
    };
#undef M
    return neighboors_mask & MASKS[f][e];
}

static bool block_get_vertice_border(uint32_t neighboors_mask, int f, int i)
{
#define M(x, y, z) (1 << ((x + 1) + (y + 1) * 3 + (z + 1) * 9))
    static const uint32_t MASKS[6][4] = {
        {   // F0
            M(-1,  0, 1) | M( 0, -1, 1) | M(-1, -1, 1),
            M( 0, -1, 1) | M( 1,  0, 1) | M( 1, -1, 1),
            M( 1,  0, 1) | M( 0,  1, 1) | M( 1,  1, 1),
            M( 0,  1, 1) | M(-1,  0, 1) | M(-1,  1, 1),
        },
        {   // F1
            M( 1,  0, -1) | M( 0, -1, -1) | M( 1, -1, -1),
            M( 0, -1, -1) | M(-1,  0, -1) | M(-1, -1, -1),
            M(-1,  0, -1) | M( 0,  1, -1) | M(-1,  1, -1),
            M( 0,  1, -1) | M( 1,  0, -1) | M( 1,  1, -1),
        },
        {   // F2
            M( 0, -1,  1) | M(-1, -1,  0) | M(-1, -1,  1),
            M(-1, -1,  0) | M( 0, -1, -1) | M(-1, -1, -1),
            M( 0, -1, -1) | M( 1, -1,  0) | M( 1, -1, -1),
            M( 1, -1,  0) | M( 0, -1,  1) | M( 1, -1,  1),
        },
        {   //F3
            M( 0,  1,  1) | M( 1,  1,  0) | M( 1, 1,  1),
            M( 1,  1,  0) | M( 0,  1, -1) | M( 1, 1, -1),
            M( 0,  1, -1) | M(-1,  1,  0) | M(-1, 1, -1),
            M(-1,  1,  0) | M( 0,  1,  1) | M(-1, 1,  1),
        },
        {   // F4
            M( 1,  0,  1) | M( 1, -1,  0) | M( 1, -1,  1),
            M( 1, -1,  0) | M( 1,  0, -1) | M( 1, -1, -1),
            M( 1,  0, -1) | M( 1,  1,  0) | M( 1,  1, -1),
            M( 1,  1,  0) | M( 1,  0,  1) | M( 1,  1,  1),
        },
        {   // F5
            M(-1, -1,  0) | M(-1,  0,  1) | M(-1, -1,  1),
            M(-1,  0,  1) | M(-1,  1,  0) | M(-1,  1,  1),
            M(-1,  1,  0) | M(-1,  0, -1) | M(-1,  1, -1),
            M(-1,  0, -1) | M(-1, -1,  0) | M(-1, -1, -1),
        },
    };
#undef M
    return neighboors_mask & MASKS[f][i];
}

static uint8_t block_get_shadow_mask(uint32_t neighboors_mask, int f)
{
    int i;
    uint8_t ret = 0;
    for (i = 0; i < 4; i++) {
        ret |= block_get_vertice_border(neighboors_mask, f, i) ? (1 << i) : 0;
        ret |= block_get_edge_border(neighboors_mask, f, i) ? (0x10 << i) : 0;
    }
    return ret;
}

static uint8_t block_get_border_mask(uint32_t neighboors_mask,
                                     int f, int effects)
{
#define M(x, y, z) (1 << ((x + 1) + (y + 1) * 3 + (z + 1) * 9))
    int e;
    int ret = 0;
    vec3b_t n;
    if (effects & EFFECT_BORDERS_ALL) return 15;
    if (!(effects & EFFECT_BORDERS)) return 0;
    for (e = 0; e < 4; e++) {
        n = FACES_NORMALS[FACES_NEIGHBORS[f][e]];
        if (!(neighboors_mask & M(n.x, n.y, n.z)))
            ret |= 1 << e;
    }
    return ret;
#undef M
}

/* Packing of block id, pos, and face:
 *
 *    x   :  4 bits
 *    y   :  4 bits
 *    z   :  4 bits
 *    pad :  1 bit
 *    face:  3 bits
 *    -------------
 *    tot : 16 bits
 *
 *    So it fits into 2 bytes, and we can feed it into a shader as a vec2.
 */
static uvec2b_t get_pos_as_vec2(int x, int y, int z, int f)
{
    return uvec2b(x << 4 | y,
                  z << 4 | f);
}

int block_generate_vertices(const block_data_t *data, int effects,
                            voxel_vertex_t *out)
{
    PROFILED
    int x, y, z, f;
    int i, nb = 0;
    uint32_t neighboors_mask;
    uint8_t shadow_mask, borders_mask;
    vec3b_t normal;
    const int ts = VOXEL_TEXTURE_SIZE;
    uint8_t neighboors[27];
    BLOCK_ITER_INSIDE(x, y, z) {
        if (DATA_AT(data, x, y, z).a < 127) continue;    // Non visible
        neighboors_mask = block_get_neighboors(data, x, y, z, neighboors);
        for (f = 0; f < 6; f++) {
            if (!block_is_face_visible(neighboors_mask, f)) continue;
            normal = block_get_normal(neighboors_mask, neighboors, f,
                     effects & EFFECT_SMOOTH);
            shadow_mask = block_get_shadow_mask(neighboors_mask, f);
            borders_mask = block_get_border_mask(neighboors_mask, f, effects);
            for (i = 0; i < 4; i++) {
                out[nb * 4 + i].pos = vec3b_add(
                        vec3b(x, y, z),
                        VERTICES_POSITIONS[FACES_VERTICES[f][i]]);
                out[nb * 4 + i].normal = normal;
                out[nb * 4 + i].color = DATA_AT(data, x, y, z);
                out[nb * 4 + i].color.a = out[nb * 4 + i].color.a ? 255 : 0;
                out[nb * 4 + i].bshadow_uv = uvec2b(
                    shadow_mask % 16 * ts + VERTICE_UV[i].x * (ts - 1),
                    shadow_mask / 16 * ts + VERTICE_UV[i].y * (ts - 1));
                // For testing:
                // This put a border bump on all the edges of the voxel.
                out[nb * 4 + i].bump_uv = uvec2b(
                    borders_mask * 16 + VERTICE_UV[i].x * (16 - 1),
                    f * 16 + VERTICE_UV[i].y * (16 - 1));
                out[nb * 4 + i].pos_data = get_pos_as_vec2(x, y, z, f);
            }
            nb++;
        }
    }
    return nb;
}

static vec3_t block_get_voxel_pos(const block_t *block, int x, int y, int z)
{
    return vec3(block->pos.x + x - BLOCK_SIZE / 2 + 0.5,
                block->pos.y + y - BLOCK_SIZE / 2 + 0.5,
                block->pos.z + z - BLOCK_SIZE / 2 + 0.5);
}

// Copy the data if there are any other blocks having reference to it.
static void block_prepare_write(block_t *block)
{
    if (block->data->ref == 1) {
        return;
    }
    block->data->ref--;
    block_data_t *data;
    data = calloc(1, sizeof(*block->data));
    memcpy(data->voxels, block->data->voxels, N * N * N * 4);
    data->ref = 1;
    block->data = data;
    block->data->id = make_id();
    goxel()->block_count++;
}

void block_fill(block_t *block,
                uvec4b_t (*get_color)(const vec3_t *pos, void *user_data),
                void *user_data)
{
    int x, y, z;
    vec3_t p;
    uvec4b_t c;
    block_prepare_write(block);
    BLOCK_ITER(x, y, z) {
        p = block_get_voxel_pos(block, x, y, z);
        c = get_color(&p, user_data);
        BLOCK_AT(block, x, y, z) = c;
    }
}

static bool can_skip(uvec4b_t v, const painter_t *p)
{
    return (v.a && (p->op == OP_ADD) && uvec4b_equal(p->color, v)) ||
            (!v.a && (p->op == OP_SUB || p->op == OP_PAINT));
}

static void apply_op(uvec4b_t *v, const painter_t *p, uint8_t k)
{
    if (p->op == OP_PAINT)
        v->rgb = uvec3b_mix(v->rgb, p->color.rgb, k / 255.);
    if (p->op == OP_ADD) {
        v->rgb = p->color.rgb;
        v->a = max(v->a, k);
    }
    if (p->op == OP_SUB)
        v->a = 0;
}

// XXX: cleanup this function.
void block_op(block_t *block, painter_t *painter, const box_t *box)
{
    int x, y, z;
    mat4_t mat = mat4_identity;
    vec3_t p, size;
    uint8_t v;
    float (*shape_func)(const vec3_t*, const vec3_t*) = painter->shape->func;

    size = box_get_size(*box);
    mat4_imul(&mat, box->mat);
    mat4_iscale(&mat, 1 / size.x, 1 / size.y, 1 / size.z);
    mat4_invert(&mat);

    mat4_itranslate(&mat, block->pos.x, block->pos.y, block->pos.z);
    mat4_itranslate(&mat, -N / 2 + 0.5, -N / 2 + 0.5, -N / 2 + 0.5);
    BLOCK_ITER(x, y, z) {
        if (can_skip(BLOCK_AT(block, x, y, z), painter)) continue;
        p = mat4_mul_vec3(mat, vec3(x, y, z));
        v = shape_func(&p, &size) * 255;
        if (v) {
            block_prepare_write(block);
            apply_op(&BLOCK_AT(block, x, y, z), painter, v);
        }
    }
}

static uvec4b_t merge(uvec4b_t a, uvec4b_t b)
{
    uvec4b_t ret;
    int alpha = a.a;
    if (b.a == 0) return a;
    if (a.a == 0) return b;
    ret.a = max(a.a, b.a);
    ret.r = (a.r * alpha + b.r * (255 - alpha)) / 256;
    ret.g = (a.g * alpha + b.g * (255 - alpha)) / 256;
    ret.b = (a.b * alpha + b.b * (255 - alpha)) / 256;
    return ret;
}

void block_merge(block_t *block, const block_t *other)
{
    int x, y, z;
    if (!other || other->data == get_empty_data()) return;
    if (block->data == get_empty_data()) {
        block_set_data(block, other->data);
        return;
    }

    block_prepare_write(block);
    BLOCK_ITER(x, y, z) {
        BLOCK_AT(block, x, y, z) = merge(DATA_AT(block->data, x, y, z),
                                         DATA_AT(other->data, x, y, z));
    }
}

uvec4b_t block_get_at(const block_t *block, const vec3_t *pos)
{
    int x, y, z;
    vec3_t p = *pos;
    assert(bbox_contains_vec(block_get_box(block, false), *pos));
    vec3_isub(&p, block->pos);
    vec3_iadd(&p, vec3(N / 2 - 0.5, N / 2 - 0.5, N / 2 - 0.5));
    x = nearbyint(p.x);
    y = nearbyint(p.y);
    z = nearbyint(p.z);
    return BLOCK_AT(block, x, y, z);
}
