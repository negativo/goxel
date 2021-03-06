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

enum {
    STATE_IDLE = 0,
    STATE_CANCEL,
    STATE_SNAPED,
    STATE_PAINT,
    STATE_PAINT2,
    STATE_WAIT_UP,
    STATE_WAIT_KEY_UP,
};

static box_t get_box(const vec3_t *p0, const vec3_t *p1, const vec3_t *n,
                     float r, const plane_t *plane)
{
    mat4_t rot;
    box_t box;
    if (p1 == NULL) {
        box = bbox_from_extents(*p0, r, r, r);
        box = box_swap_axis(box, 2, 0, 1);
        return box;
    }
    if (r == 0) {
        box = bbox_grow(bbox_from_points(*p0, *p1), 0.5, 0.5, 0.5);
        // Apply the plane rotation.
        rot = plane->mat;
        rot.vecs[3] = vec4(0, 0, 0, 1);
        mat4_imul(&box.mat, rot);
        return box;
    }

    // Create a box for a line:
    int i;
    const vec3_t AXES[] = {vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)};

    box.mat = mat4_identity;
    box.p = vec3_mix(*p0, *p1, 0.5);
    box.d = vec3_sub(*p1, box.p);
    for (i = 0; i < 3; i++) {
        box.w = vec3_cross(box.d, AXES[i]);
        if (vec3_norm2(box.w) > 0) break;
    }
    if (i == 3) return box;
    box.w = vec3_mul(vec3_normalized(box.w), r);
    box.h = vec3_mul(vec3_normalized(vec3_cross(box.d, box.w)), r);
    return box;
}

static bool check_can_skip(goxel_t *goxel, vec3_t pos, bool pressed, int op)
{
    if (    pressed == goxel->tool_last_op.pressed &&
            op == goxel->tool_last_op.op &&
            vec3_equal(pos, goxel->tool_last_op.pos))
        return true;
    goxel->tool_last_op.pressed = pressed;
    goxel->tool_last_op.op = op;
    goxel->tool_last_op.pos = pos;
    return false;
}

static int tool_cube_iter(goxel_t *goxel, const inputs_t *inputs, int state,
                          const vec2_t *view_size, bool inside)
{
    const bool down = inputs->mouse_down[0];
    const bool up = !down;
    bool snaped = false;
    vec3_t pos, pos2, normal;
    box_t box;
    uvec4b_t box_color = HEXCOLOR(0xffff00ff);
    mesh_t *mesh = goxel->image->active_layer->mesh;

    if (state != STATE_PAINT2)
        snaped = inside && goxel_unproject(goxel, view_size,
                                           &inputs->mouse_pos, &pos, &normal);
    if (snaped) {
        if (goxel->painter.op == OP_ADD) vec3_iadd(&pos, normal);
        pos.x = nearbyint(pos.x - 0.5) + 0.5;
        pos.y = nearbyint(pos.y - 0.5) + 0.5;
        pos.z = nearbyint(pos.z - 0.5) + 0.5;
    }
    switch (state) {
    case STATE_IDLE:
        goxel->tool_t = 0;
        if (snaped) state = STATE_SNAPED;
        else return state;
        // Fall through.
    case STATE_SNAPED:
        if (goxel->tool_t == 0) {
            goxel->tool_t = 1;
            mesh_set(&goxel->tool_origin_mesh, mesh);
        }
        if (!snaped) return STATE_CANCEL;
        goxel_set_help_text(goxel, "Click and drag to draw.");
        goxel->tool_start_pos = pos;
        box = get_box(&goxel->tool_start_pos, &pos, &normal, 0,
                      &goxel->plane);
        mesh_set(&mesh, goxel->tool_origin_mesh);
        mesh_op(mesh, &goxel->painter, &box);
        render_box(&goxel->rend, &box, false, &box_color);
        if (down) {
            state = STATE_PAINT;
            goxel->painting = true;
        }
        else return state;
        // Fall through.
    case STATE_PAINT:
        goxel_set_help_text(goxel, "Drag.");
        box = get_box(&goxel->tool_start_pos, &pos, &normal, 0, &goxel->plane);
        render_box(&goxel->rend, &box, false, &box_color);
        mesh_set(&mesh, goxel->tool_origin_mesh);
        mesh_op(mesh, &goxel->painter, &box);
        goxel_update_meshes(goxel, false);
        if (up) {
            goxel->tool_plane_hidden_restore = goxel->plane_hidden;
            goxel->plane_hidden = true;
            state = STATE_PAINT2;
            goxel->tool_plane = plane_from_normal(pos, goxel->plane.u);
        } else return state;
        // Fall through.
    case STATE_PAINT2:
        goxel_set_help_text(goxel, "Adjust height.");
        render_plane(&goxel->rend, &goxel->tool_plane, &goxel->grid_color);
        if (goxel_unproject_on_plane(goxel, view_size, &inputs->mouse_pos,
                                     &goxel->tool_plane, &pos2, &normal)) {
            pos2 = vec3_add(goxel->tool_plane.p,
                    vec3_project(vec3_sub(pos2, goxel->tool_plane.p),
                                 goxel->plane.n));
            pos2.x = nearbyint(pos2.x - 0.5) + 0.5;
            pos2.y = nearbyint(pos2.y - 0.5) + 0.5;
            pos2.z = nearbyint(pos2.z - 0.5) + 0.5;
            box = get_box(&goxel->tool_start_pos, &pos2, &normal, 0,
                          &goxel->plane);
            render_box(&goxel->rend, &box, false, &box_color);
            mesh_set(&mesh, goxel->tool_origin_mesh);
            mesh_op(mesh, &goxel->painter, &box);
            goxel_update_meshes(goxel, false);
        }
        if (down) {
            mesh_set(&mesh, goxel->tool_origin_mesh);
            mesh_op(mesh, &goxel->painter, &box);
            goxel_update_meshes(goxel, true);
            goxel->painting = false;
            image_history_push(goxel->image);
            return STATE_WAIT_UP;
        }
        return state;
    case STATE_WAIT_UP:
        goxel->plane_hidden = goxel->tool_plane_hidden_restore;
        return up ? STATE_IDLE : STATE_WAIT_UP;
    default:
        assert(false);
        return 0;
    }
}

static int tool_brush_iter(goxel_t *goxel, const inputs_t *inputs, int state,
                           const vec2_t *view_size, bool inside)
{
    const bool down = inputs->mouse_down[0];
    const bool pressed = down && !goxel->painting;
    const bool released = !down && goxel->painting;
    int snaped = 0;
    vec3_t pos, normal;
    box_t box;
    painter_t painter2;
    mesh_t *mesh = goxel->image->active_layer->mesh;

    if (inside)
        snaped = goxel_unproject(goxel, view_size, &inputs->mouse_pos,
                                 &pos, &normal);
    goxel_set_help_text(goxel, "Brush: use shift to draw lines");
    if (snaped) {
        if (    snaped == SNAP_MESH && goxel->painter.op == OP_ADD &&
                !goxel->snap_offset)
            vec3_iadd(&pos, normal);
        if (goxel->tool == TOOL_BRUSH && goxel->snap_offset)
            vec3_iaddk(&pos, normal, goxel->snap_offset * goxel->tool_radius);
        pos.x = nearbyint(pos.x - 0.5) + 0.5;
        pos.y = nearbyint(pos.y - 0.5) + 0.5;
        pos.z = nearbyint(pos.z - 0.5) + 0.5;
    }
    switch (state) {
    case STATE_IDLE:
        goxel->tool_t = 0;
        if (snaped) state = STATE_SNAPED;
        else return state;
        // Fall through.
    case STATE_SNAPED:
        if (goxel->tool_t == 0) {
            goxel->tool_t = 1;
            mesh_set(&goxel->tool_origin_mesh, mesh);
            if (!inputs->keys[KEY_SHIFT])
                mesh_set(&goxel->pick_mesh, goxel->layers_mesh);
            goxel->tool_last_op.op = 0; // Discard last op.
        }
        if (!snaped) return STATE_CANCEL;
        if (inputs->keys[KEY_SHIFT])
            render_line(&goxel->rend, &goxel->tool_start_pos, &pos);
        if (check_can_skip(goxel, pos, down, goxel->painter.op))
            return state;
        box = get_box(&pos, NULL, &normal, goxel->tool_radius, NULL);

        mesh_set(&mesh, goxel->tool_origin_mesh);
        mesh_op(mesh, &goxel->painter, &box);
        goxel_update_meshes(goxel, false);

        if (inputs->keys[KEY_SHIFT]) {
            render_line(&goxel->rend, &goxel->tool_start_pos, &pos);
            if (pressed) {
                painter2 = goxel->painter;
                painter2.shape = &shape_cylinder;
                box = get_box(&goxel->tool_start_pos, &pos, &normal,
                              goxel->tool_radius, NULL);
                mesh_op(mesh, &painter2, &box);
                goxel_update_meshes(goxel, false);
                goxel->tool_start_pos = pos;
                mesh_set(&goxel->tool_origin_mesh, mesh);
            }
        }
        if (pressed) {
            mesh_set(&mesh, goxel->tool_origin_mesh);
            state = STATE_PAINT;
            goxel->tool_last_op.op = 0;
            goxel->painting = true;
        }
        else return state;
        // Fall through.
    case STATE_PAINT:
        if (check_can_skip(goxel, pos, down, goxel->painter.op))
            return state;
        box = get_box(&pos, NULL, &normal, goxel->tool_radius, NULL);
        mesh_op(mesh, &goxel->painter, &box);
        goxel_update_meshes(goxel, false);
        goxel->tool_start_pos = pos;
        if (released) {
            image_history_push(goxel->image);
            goxel->painting = false;
            if (inputs->keys[KEY_SHIFT])
                return STATE_WAIT_KEY_UP;
            mesh_set(&goxel->pick_mesh, goxel->layers_mesh);
            return STATE_IDLE;
        }
        return state;
    case STATE_WAIT_KEY_UP:
        goxel->tool_t = 0;
        if (!inputs->keys[KEY_SHIFT]) state = STATE_IDLE;
        if (snaped) state = STATE_SNAPED;
        return state;
    default:
        assert(false);
        return 0;
    }
}

static int tool_laser_iter(goxel_t *goxel, const inputs_t *inputs, int state,
                           const vec2_t *view_size, bool inside)
{
    vec3_t pos, normal;
    box_t box;
    painter_t painter = goxel->painter;
    mesh_t *mesh = goxel->image->active_layer->mesh;
    const bool down = inputs->mouse_down[0];

    painter.op = OP_SUB;
    painter.shape = &shape_cylinder;
    // Create the laser box with an inifinity width.
    goxel_unproject_on_screen(goxel, view_size, &inputs->mouse_pos,
                              &pos, &normal);
    box.mat = mat4_identity;
    box.w = mat4_mul_vec(mat4_inverted(goxel->camera.view_mat),
                     vec4(1, 0, 0, 0)).xyz;
    box.h = mat4_mul_vec(mat4_inverted(goxel->camera.view_mat),
                     vec4(0, 1, 0, 0)).xyz;
    box.d = mat4_mul_vec(mat4_inverted(goxel->camera.view_mat),
                     vec4(0, 0, 1, 0)).xyz;
    box.p = pos;
    mat4_itranslate(&box.mat, 0, 0, -128);
    mat4_iscale(&box.mat, goxel->tool_radius, goxel->tool_radius, 128);
    render_box(&goxel->rend, &box, false, NULL);
    switch (state) {
    case STATE_IDLE:
        if (down) state = STATE_PAINT;
        else return state;
    case STATE_PAINT:
        if (!down) {
            image_history_push(goxel->image);
            return STATE_IDLE;
        }
        mesh_op(mesh, &painter, &box);
        goxel_update_meshes(goxel, false);
        return state;
    default:
        assert(false);
        return 0;
    }
}

static int tool_set_plane_iter(goxel_t *goxel, const inputs_t *inputs,
                               int state, const vec2_t *view_size,
                               bool inside)
{
    bool snaped;
    vec3_t pos, normal;
    mesh_t *mesh = goxel->layers_mesh;
    const bool pressed = inputs->mouse_down[0];
    goxel_set_help_text(goxel, "Click on the mesh to set plane.");
    snaped = inside && goxel_unproject_on_mesh(goxel, view_size,
                            &inputs->mouse_pos, mesh, &pos, &normal);
    if (snaped && pressed) {
        vec3_iadd(&pos, normal);
        goxel->plane = plane_from_normal(pos, normal);
    }
    return 0;
}

int tool_iter(goxel_t *goxel, int tool, const inputs_t *inputs, int state,
              const vec2_t *view_size, bool inside)
{
    int ret;
    switch (tool) {
    case TOOL_CUBE:
        ret = tool_cube_iter(goxel, inputs, state, view_size, inside);
        break;
    case TOOL_BRUSH:
        ret = tool_brush_iter(goxel, inputs, state, view_size, inside);
        break;
    case TOOL_LASER:
        ret = tool_laser_iter(goxel, inputs, state, view_size, inside);
        break;
    case TOOL_SET_PLANE:
        ret = tool_set_plane_iter(goxel, inputs, state, view_size, inside);
        break;
    case TOOL_MOVE:
        ret = 0;
        break;
    default:
        ret = 0;
        assert(false);
    }
    if (ret == STATE_CANCEL && goxel->tool_origin_mesh) {
        mesh_set(&goxel->image->active_layer->mesh, goxel->tool_origin_mesh);
        goxel_update_meshes(goxel, true);
        ret = 0;
    }
    if (ret == 0 && goxel->tool_origin_mesh) {
        mesh_delete(goxel->tool_origin_mesh);
        goxel->tool_origin_mesh = NULL;
    }
    return ret;
}

void tool_cancel(goxel_t *goxel, int tool, int state)
{
    if (state == 0) return;
    if (goxel->tool_origin_mesh) {
        mesh_set(&goxel->image->active_layer->mesh, goxel->tool_origin_mesh);
        goxel_update_meshes(goxel, true);
        mesh_delete(goxel->tool_origin_mesh);
        goxel->tool_origin_mesh = NULL;
    }
    goxel->tool_state = 0;
}
