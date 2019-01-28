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

using namespace put;

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

void update_character_controller(put::scene_controller* sc)
{
    static f32 blend = 0.0f;
    ImGui::SliderFloat("Blend", &blend, 0.0f, 1.0f);
    
    static vec3f pos = vec3f::zero();
    static vec3f dir = vec3f::unit_z();
    f32 vel = 0.0f;
    
    u32 trajectory_node = 5;
    
    vec3f xz_dir = sc->camera->focus - sc->camera->pos;
    xz_dir.y = 0.0f;
    xz_dir = normalised(xz_dir);
    
    f32 xz_angle = acos(dot(xz_dir, vec3f(0.0f, 0.0f, 1.0f)));
    
    u32 num_pads = pen::input_get_num_gamepads();
    if(num_pads > 0)
    {
        pen::gamepad_state gs;
        pen::input_get_gamepad_state(0, gs);
        
        vec3f left_stick = vec3f(gs.axis[PGP_AXIS_LEFT_STICK_X], 0.0f, gs.axis[PGP_AXIS_LEFT_STICK_Y]);
        
        mat4 rot = mat::create_y_rotation(xz_angle);
        
        vec3f transfromed_left_stick = normalised(-rot.transform_vector(left_stick));
        
        if(mag(left_stick) > 0.2f)
        {
            vel = mag(left_stick);
            dir = transfromed_left_stick;
        }
    }
    
    f32 dir_angle = atan2(dir.x, dir.z);
    
    if(sc->scene->num_nodes > trajectory_node)
    {
        quat rot;
        rot.euler_angles(0.0f, dir_angle, 0.0f);
        sc->scene->initial_transform[trajectory_node].rotation = rot;
    }
    
    sc->scene->anim_controller[1].current_time += vel * 0.01f;
    pos = sc->scene->local_matrices[1].get_translation();
    
    sc->scene->anim_controller_v2[1].blend = blend;
    
    put::dbg::add_line(pos, pos + dir, vec4f::blue());
    put::dbg::add_line(pos, pos + xz_dir);
    put::dbg::add_circle(vec3f::unit_y(), pos, 0.5f, vec4f::green());
    
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
    
    while( 1 )
    {
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
    }
    
    //clean up mem here
	put::dbg::shutdown();
	put::dev_ui::shutdown();

    pen::renderer_consume_cmd_buffer();
    
    //signal to the engine the thread has finished
    pen::semaphore_post( p_thread_info->p_sem_terminated, 1);
    
    return PEN_THREAD_OK;
}
