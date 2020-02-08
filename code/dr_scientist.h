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
#include "ecs/ecs_scene.h"
#include "ecs/ecs_resources.h"
#include "ecs/ecs_editor.h"
#include "ecs/ecs_utilities.h"
#include "data_struct.h"
#include "maths/maths.h"

using namespace put;
using namespace ecs;

namespace e_cmp_flags
{
    enum cmp_flags_t
    {
        tile_block = 1,
        custom_anim = 1<<1
    };
}

namespace e_game_flags
{
    enum game_flags_t
    {
        none = 0,
        tile_in_island = 1<<0
    };
}

namespace e_tile_flags
{
    enum tile_flags_t
    {
        none = 0,
        inner = 1<<0
    };
}

namespace e_contoller
{
    enum contoller_t
    {
        jump = 1 << 0,
        run = 1 << 1,
        debounce_jump = 1 << 2
    };
}

struct tile_block
{
    u32 neighbour_mask; //x,y,z,-x,-y,-z.
    u32 island_id = 0;
    u32 flags = 0;
};

struct dr_ecs_exts
{
    cmp_array<s32>          cmp_flags;
    cmp_array<s32>          game_flags;
    cmp_array<tile_block>   tile_blocks;

    u32                     num_components = ((size_t)&num_components - (size_t)&cmp_flags) / sizeof(generic_cmp_array);
};

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
    // tweakable constants.. todo

    // dynamic vars
    f32 loco_vel = 0.0f;
    f32 air_vel = 0.0f;
    f32 air = 0.0f;

    vec3f pps = vec3f::zero(); // prev pos
    vec3f pos = vec3f::zero();
    vec3f vel = vec3f::zero();
    vec3f acc = vec3f::zero();

    u32   actions = 0;         // flags

    vec3f surface_normal = vec3f::zero();
    vec3f surface_perp = vec3f::zero();

    // cam
    f32   cam_y_target = 0.0f;
    vec3f cam_pos_target = vec3f::zero();
    f32   cam_zoom_target;
};

void update_game_controller(ecs_controller& ecsc, ecs_scene* scene, f32 dt);
void update_game_components(ecs_extension& ext, ecs_scene* scene, f32 dt);
