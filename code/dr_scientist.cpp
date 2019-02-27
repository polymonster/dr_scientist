#include "pen.h"
#include "renderer.h"
#include "timer.h"
#include "file_system.h"
#include "volume_generator.h"
#include "pen_string.h"
#include "loader.h"
#include "dev_ui.h"
#include "camera.h"
#include "debug_render.h"
#include "pmfx.h"
#include "pen_json.h"
#include "hash.h"
#include "str_utilities.h"
#include "input.h"
#include "ces/ces_scene.h"
#include "ces/ces_resources.h"
#include "ces/ces_editor.h"
#include "ces/ces_utilities.h"
#include "data_struct.h"
#include "maths/maths.h"

using namespace put;
using namespace ces;

pen::window_creation_params pen_window
{
    1280,					//width
    720,					//height
    4,						//MSAA samples
    "dr_scientist"		    //window title / process name
};

namespace physics
{
    extern PEN_TRV physics_thread_main( void* params );
}

struct dr_char
{
    u32 root = 0;
    u32 anim_idle;
    u32 anim_walk;
    u32 anim_run;
    u32 anim_jump;
    u32 anim_run_l;
    u32 anim_run_r;
};
dr_char dr;

f32 user_thread_time = 0.0f;
void mini_profiler()
{
    f32 render_gpu = 0.0f;
    f32 render_cpu = 0.0f;
    pen::renderer_get_present_time(render_cpu, render_gpu);

    static f32 max_gpu = 0.0f;
    static f32 max_render = 0.0f;
    static f32 max_user = 0.0f;

    max_gpu = max(render_gpu, max_gpu);
    max_render = max(render_cpu, max_render);
    max_user = max(user_thread_time, max_user);

    ImGui::Separator();
    ImGui::Text("Stats:");
    ImGui::Text("User Thread: %2.2f ms", user_thread_time);
    ImGui::Text("Render Thread: %2.2f ms", render_cpu);
    ImGui::Text("GPU: %2.2f ms", render_gpu);

    ImGui::Text("max_gpu: %2.2f ms", max_gpu);
    ImGui::Text("max_render: %2.2f ms", max_render);
    ImGui::Text("max_user: %2.2f ms", max_user);

    if (ImGui::Button("Reset"))
    {
        max_gpu = 0.0f;
        max_render = 0.0f;
        max_user = 0.0f;
    }

    ImGui::Separator();
}

void add_box(put::ces::entity_scene* scene, const vec3f& pos)
{
    static material_resource* default_material = get_material_resource(PEN_HASH("default_material"));
    static geometry_resource* box = get_geometry_resource(PEN_HASH("cube"));
    
    u32 b = get_new_node(scene);
    scene->names[b] = "ground";
    scene->transforms[b].translation = pos;
    scene->transforms[b].rotation = quat();
    scene->transforms[b].scale = vec3f(0.5f);
    scene->entities[b] |= CMP_TRANSFORM;
    scene->parents[b] = b;
    scene->physics_data[b].rigid_body.shape = physics::BOX;
    scene->physics_data[b].rigid_body.mass = 0.0f;
    scene->physics_data[b].rigid_body.group = 1;
    scene->physics_data[b].rigid_body.mask = 0xffffffff;

    instantiate_geometry(box, scene, b);
    instantiate_material(default_material, scene, b);
    instantiate_model_cbuffer(scene, b);
    instantiate_rigid_body(scene, b);
}

void setup_character(put::ces::entity_scene* scene)
{
    // load main model
    dr.root = ces::load_pmm("data/models/characters/doctor/Doctor.pmm", scene);
    
    // load anims
    anim_handle idle = ces::load_pma("data/models/characters/doctor/anims/doctor_idle01.pma");
    anim_handle walk = ces::load_pma("data/models/characters/doctor/anims/doctor_walk.pma");
    anim_handle run = ces::load_pma("data/models/characters/doctor/anims/doctor_run.pma");
    anim_handle jump = ces::load_pma("data/models/characters/doctor/anims/doctor_idle_jump.pma");
    anim_handle run_l = ces::load_pma("data/models/characters/doctor/anims/doctor_run_l.pma");
    anim_handle run_r = ces::load_pma("data/models/characters/doctor/anims/doctor_run_r.pma");
    
    // bind to rig
    ces::bind_animation_to_rig(scene, idle, dr.root);
    ces::bind_animation_to_rig(scene, walk, dr.root);
    ces::bind_animation_to_rig(scene, run, dr.root);
    ces::bind_animation_to_rig(scene, jump, dr.root);
    ces::bind_animation_to_rig(scene, run_l, dr.root);
    ces::bind_animation_to_rig(scene, run_r, dr.root);

    // add capsule for collisions
    scene->physics_data[dr.root].rigid_body.shape = physics::CAPSULE;
    scene->physics_data[dr.root].rigid_body.mass = 1.0f;
    scene->physics_data[dr.root].rigid_body.group = 4;
    scene->physics_data[dr.root].rigid_body.mask = ~1;
    scene->physics_data[dr.root].rigid_body.dimensions = vec3f(0.3f, 0.3f, 0.3f);
    scene->physics_data[dr.root].rigid_body.create_flags |= (physics::CF_DIMENSIONS | physics::CF_KINEMATIC);

    // drs feet are at 0.. offset collision to centre at 0.5
    scene->physics_offset[dr.root].translation = vec3f(0.0f, 0.5f, 0.0f);

    instantiate_rigid_body(scene, dr.root);

    physics::set_v3(scene->physics_handles[dr.root], vec3f::zero(), physics::CMD_SET_ANGULAR_FACTOR);

    // todo make bind return index
    dr.anim_idle = 0;
    dr.anim_walk = 1;
    dr.anim_run = 2;
    dr.anim_jump = 3;
    dr.anim_run_l = 4;
    dr.anim_run_r = 5;
    
    // add a few quick bits of collision
    vec3f start = vec3f(-5.5f, -0.5f, -5.5f);
    vec3f pos = start;

    // a wall
    for (u32 i = 0; i < 11; ++i)
    {
        pos.z += 1.0f;
        add_box(scene, pos + vec3f(0.0f, 1.0f, 0.0f));
    }

    // floors
    for (u32 k = 0; k < 2; ++k)
    {
        pos.z = start.z;

        for (u32 i = 0; i < 11; ++i)
        {
            pos.x = start.x;

            for (u32 j = 0; j < 11; ++j)
            {
                add_box(scene, pos);
                pos.x += 1.0f;
            }

            pos.z += 1.0f;
        }

        pos.y -= 4.0f;
        start.z -= 5.0f;
    }

    static material_resource* default_material = get_material_resource(PEN_HASH("default_material"));
    static geometry_resource* box = get_geometry_resource(PEN_HASH("cube"));

    // some boxes to knock over
    pos = vec3f(-3.0, 0.5f, 2.0f);

    for (u32 k = 0; k < 4; ++k)
    {
        u32 b = get_new_node(scene);
        scene->names[b] = "box";
        scene->transforms[b].translation = pos;
        scene->transforms[b].rotation = quat();
        scene->transforms[b].scale = vec3f(0.25f);
        scene->entities[b] |= CMP_TRANSFORM;
        scene->parents[b] = b;
        scene->physics_data[b].rigid_body.shape = physics::BOX;
        scene->physics_data[b].rigid_body.mass = 0.1f;
        scene->physics_data[b].rigid_body.group = 2;
        scene->physics_data[b].rigid_body.mask = 0xffffffff;

        instantiate_geometry(box, scene, b);
        instantiate_material(default_material, scene, b);
        instantiate_model_cbuffer(scene, b);
        instantiate_rigid_body(scene, b);

        pos.x += 0.5f;
    }

    // slope
    u32 b = get_new_node(scene);
    scene->names[b] = "slope";
    scene->transforms[b].translation = vec3f(10.0f, -1.0f, 2.0f);
    scene->transforms[b].rotation = quat(M_PI * 0.07f, 0.0f, 0.0f);
    scene->transforms[b].scale = vec3f(10.0f, 0.5f, 3.0f);
    scene->entities[b] |= CMP_TRANSFORM;
    scene->parents[b] = b;
    scene->physics_data[b].rigid_body.shape = physics::BOX;
    scene->physics_data[b].rigid_body.mass = 0.0f;
    scene->physics_data[b].rigid_body.group = 1;
    scene->physics_data[b].rigid_body.mask = 0xffffffff;

    instantiate_geometry(box, scene, b);
    instantiate_material(default_material, scene, b);
    instantiate_model_cbuffer(scene, b);
    instantiate_rigid_body(scene, b);

    physics::physics_consume_command_buffer();
    pen::thread_sleep_ms(4);
}

bool can_edit()
{
    if (pen::input_key(PK_MENU))
        return false;
    
    if (dev_ui::want_capture())
        return false;
    
    return true;
}

void update_level_editor(put::scene_controller* sc)
{
    ces::entity_scene* scene = sc->scene;
    
    static bool open = false;
    static vec3i slice = vec3i(0, 0, 0);
    static vec3f pN = vec3f::unit_y();
    static vec3f p0 = vec3f::zero();
    static vec3f* grid = nullptr;
    
    ImGui::BeginMainMenuBar();
    if(ImGui::MenuItem(ICON_FA_BRIEFCASE))
        open = !open;
    ImGui::EndMainMenuBar();
    
    if(!open)
        return;
    
    ImGui::Begin("Toolbox");
    
    ImGui::InputInt("Level", &slice[1]);
    p0.y = slice.y;
    
    ImGui::End();
    
    pen::mouse_state ms = pen::input_get_mouse_state();
    
    // get a camera ray
    vec2i vpi = vec2i(pen_window.width, pen_window.height);
    ms.y = vpi.y - ms.y;
    mat4  view_proj = sc->camera->proj * sc->camera->view;
    vec3f r0 = maths::unproject_sc(vec3f(ms.x, ms.y, 0.0f), view_proj, vpi);
    vec3f r1 = maths::unproject_sc(vec3f(ms.x, ms.y, 1.0f), view_proj, vpi);
    vec3f rv = normalised(r1 - r0);
    
    // intersect with edit plane
    vec3f ip = maths::ray_plane_intersect(r0, rv, p0, pN);
    
    // snap to grid
    ip = floor(ip) + vec3f(0.5f);
    ip.y = slice.y-0.5f;
    
    if(can_edit())
    {
        static bool debounce = false;
        if(ms.buttons[PEN_MOUSE_L])
        {
            if(!debounce)
            {
                add_box(scene, ip);
                
                debounce = true;
            }
        }
        else
        {
            debounce = false;
        }
        
        put::dbg::add_aabb(ip - vec3f(0.5f), ip + vec3f(0.5f));
    }
    
    u32 num_boxes = sb_count(grid);
    for(u32 i = 0; i < num_boxes; ++i)
    {
        put::dbg::add_aabb(grid[i] - vec3f(0.5f), grid[i] + vec3f(0.5f), vec4f::red());
    }
}

enum e_contoller_actions
{
    JUMP = 1<<0,
    RUN = 1<<1,
    DEBOUNCE_JUMP = 1<<2
};

struct controller_input
{
    vec3f movement_dir = vec3f(0.0f, 0.0f, 1.0f);
    f32   dir_angle = 0.0f;
    f32   movement_vel = 0.0f;
    vec2f cam_rot = vec2f::zero();
    u8    actions = 0;
};

struct player_controller
{
    // tweakable constants

    // dynamic vars
    f32 loco_vel = 0.0f;
    f32 air_vel = 0.0f;
    f32 air = 0.0f;

    vec3f pps = vec3f::zero(); // prev pos
    vec3f pos = vec3f::zero();
    vec3f vel = vec3f::zero();
    vec3f acc = vec3f::zero();

    f32   cam_y_target = 0.0f;
    vec3f cam_pos_target = vec3f::zero();
    f32   cam_zoom_target;

    u32   actions = 0;
};

void get_controller_input(put::scene_controller* sc, controller_input& ci)
{
    // clear state
    ci.actions = 0;
    
    vec3f left_stick = vec3f::zero();
    vec2f right_stick = vec2f::zero();
    
    vec3f xz_dir = sc->camera->focus - sc->camera->pos;
    xz_dir.y = 0.0f;
    xz_dir = normalised(xz_dir);
    
    f32 xz_angle = atan2(xz_dir.x, xz_dir.z);
    
    u32 num_pads = pen::input_get_num_gamepads();
    if (num_pads > 0)
    {
        pen::gamepad_state gs;
        pen::input_get_gamepad_state(0, gs);
        
        left_stick = vec3f(gs.axis[PGP_AXIS_LEFT_STICK_X], 0.0f, gs.axis[PGP_AXIS_LEFT_STICK_Y]);
        right_stick = vec2f(gs.axis[PGP_AXIS_RIGHT_STICK_X], gs.axis[PGP_AXIS_RIGHT_STICK_Y]);
        
        ci.cam_rot = vec2f(-right_stick.y, right_stick.x);
        
        if(gs.button[PGP_BUTTON_X])
        {
            ci.actions |= RUN;
        }
        
        if(gs.button[PGP_BUTTON_A])
        {
            ci.actions |= JUMP;
        }
    }
    else
    {
        if (pen::input_key(PK_W))
        {
            left_stick.z = -1.0f;
        }
        else if (pen::input_key(PK_S))
        {
            left_stick.z = 1.0f;
        }
        
        if (pen::input_key(PK_A))
        {
            left_stick.x = -1.0f;
        }
        else if (pen::input_key(PK_D))
        {
            left_stick.x = 1.0f;
        }
        
        if (pen::input_key(PK_Q))
        {
             ci.actions |= JUMP;
        }
        
        if (pen::input_key(PK_SHIFT))
        {
            ci.actions |= RUN;
        }
        
        if(mag2(left_stick) > 0)
            normalise(left_stick);
    }
    
    mat4 rot = mat::create_y_rotation(xz_angle);
    
    if(mag(left_stick) > 0.2f)
    {
        vec3f transfromed_left_stick = normalised(-rot.transform_vector(left_stick));
        ci.movement_dir = transfromed_left_stick;
        ci.movement_vel = mag(left_stick);
    }
    else
    {
        ci.movement_vel = 0.0f;
    }
    
    ci.dir_angle = atan2(ci.movement_dir.x, ci.movement_dir.z);
}

struct character_cast
{
    vec3f   pos = vec3f::zero();
    vec3f   normal = vec3f::zero();
    bool    set = false;
};

void sccb(const physics::sphere_cast_result& result)
{
    character_cast* cc = (character_cast*)result.user_data;
    
    cc->pos = result.point;
    cc->normal = result.normal;

    if(result.physics_handle != PEN_INVALID_HANDLE)
        cc->set = true;
}

void rccb(const physics::ray_cast_result& result)
{
    character_cast* cc = (character_cast*)result.user_data;

    cc->pos = result.point;
    cc->normal = result.normal;

    if (result.physics_handle != PEN_INVALID_HANDLE)
        cc->set = true;
}

physics::contact* contacts = nullptr;
void ctcb(const physics::contact_test_results& results)
{
    if(contacts)
        sb_free(contacts);
    
    contacts = results.contacts;
}

void update_character_controller(put::scene_controller* sc)
{
    sc->dt = min(sc->dt, 1.0f / 10.0f);

    // tweakers and vars, move into struct

    static controller_input ci;
    static player_controller pc;
    
    static f32 gravity_strength = 18.0f;
    static f32 jump_time = 0.2f;
    static f32 jump_strength = 50.0f;
    static f32 min_jump_vel = 20.0f;
    static f32 max_jump_vel = 45.0f;
    static f32 min_zoom = 5.0f;
    static f32 max_zoom = 10.0f;
    static f32 camera_lerp = 3.0f;
    static f32 camera_lerp_y = 1.0f;
    static f32 capsule_radius = 0.3f;
    
    static bool debug_lines = false;
    static bool game_cam = false;

    // controller ----------------------------------------------------------------------------------------------------------
    
    get_controller_input(sc, ci);
    
    if (ci.movement_vel >= 0.2)
    {
        pc.loco_vel += ci.movement_vel * 0.1f;
        pc.loco_vel = min(pc.loco_vel, 5.0f);
    }
    else
    {
        pc.loco_vel *= 0.1f; // inertia
        if(pc.loco_vel < 0.001f)
            pc.loco_vel = 0.0f;
    }

    // update state --------------------------------------------------------------------------------------------------------
    
    ces::cmp_anim_controller_v2& controller = sc->scene->anim_controller_v2[dr.root];
    
    pc.acc = vec3f(0.0f, -gravity_strength, 0.0f);

    if(pc.loco_vel == 0.0f)
    {
        // idle state
        controller.blend.anim_a = dr.anim_idle;
        controller.blend.anim_b = dr.anim_walk;
    }
    else
    {
        // locomotion state
        controller.blend.anim_a = dr.anim_walk;
        controller.blend.anim_b = dr.anim_run;

        controller.blend.ratio = smooth_step(pc.loco_vel, 0.0f, 5.0f, 0.0f, 1.0f);

        if (controller.blend.ratio >= 1.0)
            controller.blend.anim_a = dr.anim_run;
        else if (controller.blend.ratio <= 0.0)
            controller.blend.anim_a = dr.anim_walk;
    }

    // reset debounce jump
    if (pc.air == 0 && !(ci.actions & JUMP))
        pc.actions &= ~DEBOUNCE_JUMP;

    // must debounce
    if (pc.actions & DEBOUNCE_JUMP)
        ci.actions &= ~JUMP;
    
    if (ci.actions & JUMP && pc.air <= jump_time)
    {
        pc.acc.y += jump_strength;
        
        if(pc.air == 0.0f)
        {
            pc.air_vel = smooth_step(pc.loco_vel, 0.0f, 5.0f, min_jump_vel, max_jump_vel);
        }
        
        pc.air = max(pc.air, sc->dt);
    }
    else if(ci.actions & JUMP)
    {
        // must release to re-jump
        pc.actions |= DEBOUNCE_JUMP;
    }

    if (pc.air > 0.0f)
    {
        pc.acc += ci.movement_dir * ci.movement_vel * pc.air_vel;

        if (pc.air > 1.0f/10.0f)
        {
            controller.blend.anim_a = dr.anim_jump;
            controller.blend.anim_b = dr.anim_jump;
        }
    }

    // euler integration

    // update pos from anim
    pc.pps = pc.pos;
    pc.pos = sc->scene->transforms[dr.root].translation;

    f32 dtt = sc->dt / (1.0f / 60.0f);
    f32 ar = pow(0.8f, dtt);
    
    if (!(sc->scene->flags & PAUSE_UPDATE))
    {
        pc.vel *= vec3f(ar, 1.0f, ar);
        pc.vel += pc.acc * sc->dt;
        pc.pos += pc.vel * sc->dt;
    }

    // resolve collisions ---------------------------------------------------------------------------------------------------

    vec3f mid = pc.pos + vec3f(0.0f, 0.5f, 0.0f);       // mid pos
    vec3f head = pc.pos + vec3f(0.0f, 0.65f, 0.0f);     // position of head sphere
    vec3f feet = pc.pps + vec3f(0.0f, 0.35f, 0.0f);     // position of lower sphere

    // casts
    character_cast wall_cast;
    character_cast surface_cast;
        
    physics::sphere_cast_params scp;
    scp.from = mid - ci.movement_dir * 0.3f;
    scp.to = mid + ci.movement_dir * 1000.0f;
    scp.dimension = vec3f(0.33f);
    scp.callback = &sccb;
    scp.user_data = &wall_cast;
    scp.group = 1;
    scp.mask = 1;

    physics::cast_sphere(scp, true);
    
    physics::ray_cast_params rcp;
    rcp.start = feet;
    rcp.end = feet + vec3f(0.0f, -10000.0f, 0.0f);
    rcp.callback = &rccb;
    rcp.user_data = &surface_cast;
    rcp.group = 1;
    rcp.mask = 0xff;

    physics::cast_ray(rcp, true);
    
    // walls
    vec3f cv = mid - wall_cast.pos;
    if (mag(cv) < 0.33f && wall_cast.set)
    {
        f32 diff = 0.33f - mag(cv);
        pc.pos += wall_cast.normal * diff;
    }
    
    // floor collision from casts
    f32 cvm2 = mag(mid - surface_cast.pos);
    f32 dp = dot(surface_cast.normal, vec3f::unit_y());
    if (cvm2 <= 0.5f && dp > 0.7f && pc.vel.y <= 0.0f && surface_cast.set)
    {
        pc.air = 0.0f;
        f32 diff = 0.5f - cvm2;
        pc.pos += vec3f::unit_y() * diff;
        pc.vel.y = 0.0f;
    }
    else
    {
        // in air
        pc.air += sc->dt;
    }

    // get contacts
    physics::contact_test_params ctp;

    ctp.entity = sc->scene->physics_handles[dr.root];
    ctp.callback = ctcb;

    physics::contact_test(ctp, true);
    
    // resolve overlaps
    u32 num_contacts = sb_count(contacts);
    for(u32 i = 0; i < num_contacts; ++i)
    {
        vec3f& p = contacts[i].pos;
        vec3f& n = contacts[i].normal;

        vec3f pxz = vec3f(p.x, 0.0f, p.z);
        vec3f qxz = vec3f(pc.pos.x, 0.0f, pc.pos.z);
        f32 d = dot(contacts[i].normal, vec3f::unit_y());

        // walls
        f32 m = mag(pxz - qxz);
        if(m < capsule_radius)
        {
            if(abs(d) < 0.7f)
            {
                f32 diff = capsule_radius - m;
                pc.pos += contacts[i].normal * diff;
            }
            else if(n.y > 0.0f && cvm2 > 0.6f)
            {
                // floor edges
                f32 dc = 1.0 - abs(pc.pos.y + 0.35f - p.y); // distance to cylinder part of the capsule
                f32 rad = dc * 0.33f;
                f32 diff = rad - m;

                pc.pos += normalised(contacts[i].normal * vec3f(1.0f, 0.0f, 1.0f)) * diff;
            }
        }

        //ceils
        vec3f top = pc.pos + vec3f(0.0f, 0.65f, 0.0f);
        m = mag(top - p);
        if (m < capsule_radius && p.y > top.y)
        {
            // ceil
            f32 diff = capsule_radius - m;
            pc.pos += normalised(contacts[i].normal) * diff;
            pc.vel.y *= 0.5f;
        }
    }
   
    // set onto entity
    if (!(sc->scene->flags & PAUSE_UPDATE))
    {
        PEN_ASSERT(!std::isnan(ci.dir_angle));

        for(u32 i = 0; i < 3; ++i)
            PEN_ASSERT(!std::isnan(pc.pos[i]));

        sc->scene->initial_transform[5].rotation = quat(0.0f, ci.dir_angle, 0.0f);
        sc->scene->transforms[dr.root].translation = pc.pos;
        sc->scene->entities[dr.root] |= CMP_TRANSFORM;
    }

    // camera ---------------------------------------------------------------------------------------------------------------

    if( game_cam )
    {
        f32 zl = smooth_step(pc.loco_vel, 0.0f, 5.0f, 0.0f, 1.0f);

        pc.cam_y_target = lerp(pc.cam_y_target, pc.pos.y, camera_lerp_y * sc->dt);
        pc.cam_pos_target = vec3f(pc.pos.x, pc.cam_y_target, pc.pos.z);
        pc.cam_zoom_target = lerp(min_zoom, max_zoom, zl);

        sc->camera->focus = lerp(sc->camera->focus, pc.cam_pos_target, camera_lerp * sc->dt);
        sc->camera->zoom = lerp(sc->camera->zoom, pc.cam_zoom_target, camera_lerp * sc->dt);

        if(mag(ci.cam_rot) > 0.2f)
            sc->camera->rot += (ci.cam_rot * fabs(ci.cam_rot)) * sc->dt;

        static f32 ulimit = -(M_PI / 2.0f) - M_PI * 0.02f;
        static f32 llimit = -M_PI * 0.02f;
        sc->camera->rot.x = min(max(sc->camera->rot.x, ulimit), llimit);
    }
    
    // debug crap -----------------------------------------------------------------------------------------------------------
    
    if (pen::input_key(PK_O))
        pc.vel = vec3f::zero();
    
    if(debug_lines)
    {
        put::dbg::add_point(surface_cast.pos, 0.1f, vec4f::green());

        put::dbg::add_circle(vec3f::unit_y(), mid, 0.3f, vec4f::green());
        put::dbg::add_point(wall_cast.pos, 0.1f, vec4f::green());

        put::dbg::add_point(head, 0.3f, vec4f::magenta());
        put::dbg::add_point(feet, 0.3f, vec4f::magenta());
               
        vec3f xz_dir = sc->camera->focus - sc->camera->pos;
        xz_dir.y = 0.0f;
        xz_dir = normalised(xz_dir);

        put::dbg::add_line(mid, mid + xz_dir, vec4f::white());
    }
    
    ImGui::Checkbox("debug_lines", &debug_lines);
    ImGui::Checkbox("game_cam", &game_cam);
    
    ImGui::InputFloat("gravity", &gravity_strength);
    ImGui::InputFloat("jump_time", &jump_time);
    ImGui::InputFloat("jump_vel", &jump_strength);
    ImGui::InputFloat("min_jump_vel", &min_jump_vel);
    ImGui::InputFloat("max_jump_vel", &max_jump_vel);

    ImGui::InputFloat("min_zoom", &min_zoom);
    ImGui::InputFloat("max_zoom", &max_zoom);
    ImGui::InputFloat("camera_lerp", &camera_lerp);

    ImGui::Separator();
    
    ImGui::InputFloat("pc.loco_vel", &pc.loco_vel);
    ImGui::InputFloat("pc.air", &pc.air);
    ImGui::InputFloat3("pc.vel", &pc.vel[0]);
    ImGui::InputFloat3("pc.acc", &pc.acc[0]);
    ImGui::InputFloat3("pc.pos", &pc.pos[0]);

    ImGui::InputFloat3("ci.movement_dir", &ci.movement_dir[0]);
    ImGui::InputFloat("ci.movement_vel", &ci.movement_vel);
    ImGui::InputFloat2("cam.rot", &sc->camera->rot[0]);
}

PEN_TRV pen::user_entry( void* params )
{
    //unpack the params passed to the thread and signal to the engine it ok to proceed
    pen::job_thread_params* job_params = (pen::job_thread_params*)params;
    pen::job* p_thread_info = job_params->job_info;
    pen::semaphore_post(p_thread_info->p_sem_continue, 1);
    
    pen::jobs_create_job( physics::physics_thread_main, 1024*10, nullptr, pen::THREAD_START_DETACHED );
    
	put::dev_ui::init();
	put::dbg::init();
    
	//create main camera and controller
	put::camera main_camera;
	put::camera_create_perspective( &main_camera, 60.0f, (f32)pen_window.width / (f32)pen_window.height, 0.1f, 1000.0f );

    //create the main scene and controller
    put::ces::entity_scene* main_scene = put::ces::create_scene("main_scene");
    put::ces::editor_init( main_scene );
    
	put::scene_controller cc;
	cc.camera = &main_camera;
	cc.update_function = &ces::update_model_viewer_camera;
	cc.name = "model_viewer_camera";
	cc.id_name = PEN_HASH(cc.name.c_str());
	cc.scene = main_scene;
    
    put::scene_controller character_controller;
    character_controller.camera = &main_camera;
    character_controller.update_function = &update_character_controller;
    character_controller.name = "model_viewer_camera";
    character_controller.id_name = PEN_HASH(character_controller.name.c_str());
    character_controller.scene = main_scene;
    
    put::scene_controller level_editor;
    level_editor.camera = &main_camera;
    level_editor.update_function = &update_level_editor;
    level_editor.name = "model_viewer_camera";
    level_editor.id_name = PEN_HASH(level_editor.name.c_str());
    level_editor.scene = main_scene;

    put::scene_controller sc;
    sc.scene = main_scene;
    sc.update_function = &ces::update_model_viewer_scene;
    sc.name = "main_scene";
    sc.id_name = PEN_HASH(sc.name.c_str());
	sc.camera = &main_camera;
    
    //create view renderers
    put::scene_view_renderer svr_main;
    svr_main.name = "ces_render_scene";
    svr_main.id_name = PEN_HASH(svr_main.name.c_str());
    svr_main.render_function = &ces::render_scene_view;
    
    put::scene_view_renderer svr_editor;
    svr_editor.name = "ces_render_editor";
    svr_editor.id_name = PEN_HASH(svr_editor.name.c_str());
    svr_editor.render_function = &ces::render_scene_editor;
    
    pmfx::register_scene_view_renderer(svr_main);
    pmfx::register_scene_view_renderer(svr_editor);

    pmfx::register_scene_controller(character_controller);
    pmfx::register_scene_controller(level_editor);
    pmfx::register_scene_controller(sc);
    pmfx::register_scene_controller(cc);
    
	put::vgt::init(main_scene);
    
    pmfx::init("data/configs/editor_renderer.jsn");
    
    setup_character(main_scene);
    
    while( 1 )
    {
        static u32 frame_timer = pen::timer_create("user_thread");
        pen::timer_start(frame_timer);

		put::dev_ui::new_frame();
        
        pmfx::update();
        
        pmfx::render();
        
        pmfx::show_dev_ui();
		put::vgt::show_dev_ui();
        put::dev_ui::console();
        put::dev_ui::render();
        
        pen::renderer_present();
        pen::renderer_consume_cmd_buffer();
        
		pmfx::poll_for_changes();
		put::poll_hot_loader();

        //msg from the engine we want to terminate
        if( pen::semaphore_try_wait( p_thread_info->p_sem_exit ) )
            break;

        user_thread_time = pen::timer_elapsed_ms(frame_timer);
    }
    
    //clean up mem here
	put::dbg::shutdown();
	put::dev_ui::shutdown();

    pen::renderer_consume_cmd_buffer();
    
    //signal to the engine the thread has finished
    pen::semaphore_post( p_thread_info->p_sem_terminated, 1);
    
    return PEN_THREAD_OK;
}
