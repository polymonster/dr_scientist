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
    
    // bind to rig
    ces::bind_animation_to_rig_v2(scene, idle, dr.root);
    ces::bind_animation_to_rig_v2(scene, walk, dr.root);
    ces::bind_animation_to_rig_v2(scene, run, dr.root);
    
    // todo make bind return index
    dr.anim_idle = 0;
    dr.anim_walk = 1;
    dr.anim_run = 2;
    
    vec3f platform[] = {
        vec3f(-0.5f, -0.5f, -0.5f),
        vec3f(0.5f, -0.5f, -0.5f),
        vec3f(-0.5f, -0.5f, 0.5f),
        vec3f(0.5f, -0.5f, 0.5f)
    };
    
    for(u32 i = 0; i < PEN_ARRAY_SIZE(platform); ++i)
    {
        add_box(scene, platform[i]);
    }
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

void control_character(put::scene_controller* sc, f32& dir, f32& vel, vec3f& v_dir)
{
    vec3f left_stick = vec3f::zero();

    vec3f xz_dir = sc->camera->focus - sc->camera->pos;
    xz_dir.y = 0.0f;
    xz_dir = normalised(xz_dir);

    f32 xz_angle = acos(dot(xz_dir, vec3f(0.0f, 0.0f, 1.0f)));

    u32 num_pads = pen::input_get_num_gamepads();
    if (num_pads > 0)
    {
        pen::gamepad_state gs;
        pen::input_get_gamepad_state(0, gs);

        left_stick = vec3f(gs.axis[PGP_AXIS_LEFT_STICK_X], 0.0f, gs.axis[PGP_AXIS_LEFT_STICK_Y]);
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

        normalise(left_stick);
    }

    mat4 rot = mat::create_y_rotation(xz_angle);

    vec3f transfromed_left_stick = normalised(-rot.transform_vector(left_stick));


    if (mag(left_stick) > 0.2f)
    {
        vel = mag(left_stick);
        v_dir = transfromed_left_stick;
    }

    dir = atan2(v_dir.x, v_dir.z);
}

struct character_cast
{
    vec3f pos;
    vec3f normal;
};

void sccb(const physics::sphere_cast_result& result)
{
    character_cast* cc = (character_cast*)result.user_data;
    
    cc->pos = result.point;
    cc->normal = result.normal;
}

void rccb(const physics::ray_cast_result& result)
{
    character_cast* cc = (character_cast*)result.user_data;

    cc->pos = result.point;
    cc->normal = result.normal;
}

void update_character_controller(put::scene_controller* sc)
{
    static vec3f pos = vec3f::zero();

    u32 trajectory_node = 5;
            
    f32 dir_angle = 0.0f;
    f32 vel = 0.0f;
    static vec3f v_dir = vec3f::unit_z();
    control_character(sc, dir_angle, vel, v_dir);

    if(sc->scene->num_nodes > trajectory_node)
    {
        quat rot;
        rot.euler_angles(0.0f, dir_angle, 0.0f);
        sc->scene->initial_transform[trajectory_node].rotation = rot;
    }
    
    ces::cmp_anim_controller_v2& controller = sc->scene->anim_controller_v2[dr.root];
    
    controller.blend.ratio = abs(vel);

    if (vel > 0.1)
    {
        // walk state
        controller.blend.anim_a = dr.anim_walk;
        controller.blend.anim_b = dr.anim_walk;
    }
    else
    {
        // idle state
        controller.blend.anim_a = dr.anim_idle;
        controller.blend.anim_b = dr.anim_walk;
    }

    // todo move to its own update
    update_level_editor(sc);
    
    // debug
    pos = sc->scene->world_matrices[dr.root].get_translation();
    pos -= vec3f(0.0f, 0.1f, 0.0f);
    
    sc->scene->transforms[dr.root].translation = pos;
    sc->scene->entities[dr.root] |= CMP_TRANSFORM;
    
    vec3f r0 = sc->scene->transforms[dr.root].translation + vec3f(0.0f, 0.5f, 0.0f);
    
    character_cast wall_cast;
    character_cast floor_cast;
    character_cast surface_cast;

    put::dbg::add_line(pos, pos + v_dir, vec4f::blue());
    put::dbg::add_circle(vec3f::unit_y(), pos, 0.5f, vec4f::green());

    // todo make this character controller

    physics::sphere_cast_params scp;
    scp.from = r0;
    scp.to = r0 + v_dir * 1000.0f;
    scp.dimension = vec3f(0.3f);
    scp.callback = &sccb;
    scp.user_data = &wall_cast;

    physics::cast_sphere(scp, true);
    
    scp.dimension = vec3f(0.2f);
    scp.to = r0 + vec3f(0.0f, -10000.0f, 0.0f);
    scp.user_data = &floor_cast;
    
    physics::cast_sphere(scp, true);

    physics::ray_cast_params rcp;
    rcp.start = r0;
    rcp.end = r0 + vec3f(0.0f, -10000.0f, 0.0f);
    rcp.callback = &rccb;
    rcp.user_data = &surface_cast;

    physics::cast_ray(rcp, true);
    
    // wall collisions
    vec3f cv = r0 - wall_cast.pos;
    if (mag(cv) < 0.5f)
    {
        f32 diff = 0.5f - mag(cv);
        
        sc->scene->transforms[dr.root].translation += normalised(cv) * diff;
    }
    
    // floor collision
    f32 cvm = mag(r0 - floor_cast.pos);
    if (cvm < 0.5f)
    {
        f32 cvm2 = mag(r0 - surface_cast.pos);

        if (cvm2 < 0.5f)
        {
            f32 diff = 0.5f - cvm2;
            sc->scene->transforms[dr.root].translation += vec3f::unit_y() * diff;
        }
        else
        {
            f32 diff = 0.5f - cvm;
            sc->scene->transforms[dr.root].translation += floor_cast.normal * diff;
        }
    }

    put::dbg::add_point(surface_cast.pos, 0.1f, vec4f::green());
    put::dbg::add_point(wall_cast.pos, 0.1f, vec4f::green());
    put::dbg::add_point(floor_cast.pos, 0.1f, vec4f::blue());

    put::dbg::add_line(floor_cast.pos, floor_cast.pos + floor_cast.normal, vec4f::magenta());
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
