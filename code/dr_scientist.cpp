#include "dr_scientist.h"

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

namespace
{
    // loose..
    dr_char dr;
    f32 user_thread_time = 0.0f;
}

void dr_scene_browser_ui(ecs_extension& extension, ecs_scene* scene)
{
    dr_ecs_exts* ext = (dr_ecs_exts*)extension.context;

    if (sb_count(scene->selection_list) == 1)
    {
        s32 si = scene->selection_list[0];

        if (ImGui::CollapsingHeader("Game Compnent Flags"))
        {
            ImGui::InputInt("Flags", &ext->cmp_flags[si]);
        }
        
        if (ImGui::CollapsingHeader("Game Flags"))
        {
            ImGui::InputInt("Flags", &ext->game_flags[si]);
        }
        
        if (ImGui::CollapsingHeader("Tile Blocks"))
        {
            ImGui::InputInt("Mask", (s32*)&ext->tile_blocks[si].neighbour_mask);
        }
    }
}

void* dr_ecs_extension(ecs_scene* scene)
{
    dr_ecs_exts* exts = new dr_ecs_exts();

    ecs_extension ext;
    ext.name = "dr_scientist";
    ext.id_name = PEN_HASH(ext.name);
    ext.components = (generic_cmp_array*)&exts->cmp_flags;
    ext.context = exts;
    ext.num_components = exts->num_components;
    ext.ext_func = &dr_ecs_extension;
    ext.update_func = &update_game_components;
    ext.browser_func = &dr_scene_browser_ui;

    register_ecs_extentsions(scene, ext);

    return (void*)exts;
}

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

void add_tile_block(put::ecs::ecs_scene* scene, dr_ecs_exts* ext, const vec3f& pos)
{
    static u32 p = get_new_node(scene);
    scene->transforms[p].translation = vec3f::zero();
    scene->transforms[p].rotation = quat();
    scene->transforms[p].scale = vec3f(1.0f);
    scene->entities[p] |= CMP_TRANSFORM;
    scene->names[p] = "basic_level";

    static material_resource* default_material = get_material_resource(PEN_HASH("default_material"));
    static geometry_resource* box = get_geometry_resource(PEN_HASH("cube"));
    
    u32 b = get_new_node(scene);
    scene->names[b] = "ground";
    scene->transforms[b].translation = pos;
    scene->transforms[b].rotation = quat();
    scene->transforms[b].scale = vec3f(0.5f);
    scene->entities[b] |= CMP_TRANSFORM;
    scene->parents[b] = p;
    scene->physics_data[b].rigid_body.shape = physics::BOX;
    scene->physics_data[b].rigid_body.mass = 0.0f;
    scene->physics_data[b].rigid_body.group = 1;
    scene->physics_data[b].rigid_body.mask = 0xffffffff;

    // ext flags
    ext->cmp_flags[b] |= CMP_TILE_BLOCK;

    instantiate_geometry(box, scene, b);
    instantiate_material(default_material, scene, b);
    instantiate_model_cbuffer(scene, b);
    instantiate_rigid_body(scene, b);
}

void tilemap_ray_cast(const physics::ray_cast_result& result)
{
    cast_result* cc = (cast_result*)result.user_data;

    cc->pos = result.point;
    cc->normal = result.normal;

    if (result.physics_handle != PEN_INVALID_HANDLE)
        cc->set = true;
}

void bake_tile_blocks(put::ecs::ecs_scene* scene, dr_ecs_exts* ext)
{
    Str file_top_corner = "data/models/environments/general/basic_top_corner.pmm";
    Str file_middle_corner = "data/models/environments/general/basic_middle_corner.pmm";
    Str file_top_side = "data/models/environments/general/basic_top_side.pmm";
    Str file_middle_side = "data/models/environments/general/basic_middle_side.pmm";
    Str file_top_centre = "data/models/environments/general/basic_top_center.pmm";
    
    static const f32 r90 = M_PI/2.0f;
    static const f32 r180 = M_PI;
    
    f32 tile_size = 0.5f;
    f32 sub_tile_size = 0.125f;
    
    u32 parent = get_new_node(scene);
    scene->transforms[parent].translation = vec3f::zero();
    scene->transforms[parent].rotation = quat(0.0f, 0.0f, 0.0f);
    scene->transforms[parent].scale = vec3f::one();
    scene->entities[parent] |= CMP_TRANSFORM;
    
    u32 start = scene->num_nodes;
    
    for (u32 n = 0; n < scene->num_nodes; ++n)
    {
        if (!(ext->cmp_flags[n] & CMP_TILE_BLOCK))
            continue;

        scene->state_flags[n] |= SF_HIDDEN;

        // do casts to get neighbours
        
        // cube axis
        static vec3f np[6] = {
             vec3f::unit_x(),
             vec3f::unit_y(),
             vec3f::unit_z(),
            -vec3f::unit_x(),
            -vec3f::unit_y(),
            -vec3f::unit_z()
        };
        
        // axis tangent
        static vec3f nt[6] = {
            vec3f::unit_y(),
            vec3f::unit_x(),
            vec3f::unit_x(),
            vec3f::unit_y(),
            vec3f::unit_x(),
            vec3f::unit_x()
        };
        
        // axis bi tangent
        static vec3f nbt[6] = {
            -vec3f::unit_z(),
            vec3f::unit_z(),
            vec3f::unit_y(),
            vec3f::unit_z(),
            -vec3f::unit_z(),
            -vec3f::unit_y()
        };
        
        bool neighbour[6] = { 0 };
        u32 nc = 0;

        vec3f pos = scene->transforms[n].translation;

        for (u32 b = 0; b < 6; ++b)
        {
            vec3f nip = pos + np[b];

            cast_result cast;

            physics::ray_cast_params rcp;
            rcp.start = pos;
            rcp.end = nip;
            rcp.callback = &tilemap_ray_cast;
            rcp.user_data = &cast;
            rcp.group = 1;
            rcp.mask = 1;

            physics::cast_ray(rcp, true);

            // ..
            if (cast.set)
            {
                neighbour[b] = true;
                nc++;
            }
        }

        // centre block simply gets removed
        if (nc == 6)
            continue;

        // piece can be a corner, an edge or a face in a 4x4x4 cube

        // 8 corner tiles
        static vec3f corner[8] = {
            vec3f(-1.0f,  1.0f, -1.0f),
            vec3f(-1.0f,  1.0f,  1.0f),
            vec3f( 1.0f,  1.0f,  1.0f),
            vec3f( 1.0f,  1.0f, -1.0f),
            vec3f(-1.0f, -1.0f, -1.0f),
            vec3f(-1.0f, -1.0f,  1.0f),
            vec3f( 1.0f, -1.0f,  1.0f),
            vec3f( 1.0f, -1.0f, -1.0f),
        };

        bool cn[8][3] = {
            { neighbour[3], neighbour[1], neighbour[5] },
            { neighbour[3], neighbour[1], neighbour[2] },
            { neighbour[0], neighbour[1], neighbour[2] },
            { neighbour[0], neighbour[1], neighbour[5] },
            { neighbour[3], neighbour[4], neighbour[5] },
            { neighbour[3], neighbour[4], neighbour[2] },
            { neighbour[0], neighbour[4], neighbour[2] },
            { neighbour[0], neighbour[4], neighbour[5] }
        };
        
        static quat corner_rotation[] = {
            quat(0.0f, r180, 0.0f),
            quat(0.0f, -r90, 0.0f),
            quat(0.0f, 0.0f, 0.0f),
            quat(0.0f, r90, 0.0f),
            quat(0.0f, 0.0f, r180) * quat(0.0f, -r90, 0.0f),
            quat(0.0f, 0.0f, r180) * quat(0.0f, r180, 0.0f),
            quat(0.0f, 0.0f, r180) * quat(0.0f, r90, 0.0f),
            quat(0.0f, 0.0f, r180)
        };
        
        quat corner_face_rotations[8][3] = {
            { { quat(0.0f, r180, 0.0f) }, { quat(0.0f, 0.0f, 0.0f) }, { quat(0.0f, r90, 0.0f) } },     // -x -z .
            { { quat(0.0f, r180, 0.0f) }, { quat(0.0f, 0.0f, 0.0f) }, { quat(0.0f, -r90, 0.0f) } },    // -x +z .
            { { quat(0.0f, 0.0f, 0.0f) }, { quat(0.0f, 0.0f, 0.0f) }, { quat(0.0f, -r90, 0.0f) } },    // +x +z .
            { { quat(0.0f, 0.0f, 0.0f) }, { quat(0.0f, 0.0f, 0.0f) }, { quat(0.0f, r90, 0.0f) } },     // +x -z .
            { { quat(0.0f, r180, 0.0f) }, { quat(0.0f, 0.0f, r180) }, { quat(0.0f, r90, 0.0f) } },     // .
            { { quat(0.0f, r180, 0.0f) }, { quat(0.0f, 0.0f, r180) }, { quat(0.0f, -r90, 0.0f) } },    // .
            { { quat(0.0f, 0.0f, 0.0f) }, { quat(0.0f, 0.0f, r180) }, { quat(0.0f, -r90, 0.0f) } },    // .
            { { quat(0.0f, 0.0f, 0.0f) }, { quat(0.0f, 0.0f, r180) }, { quat(0.0f, r90, 0.0f) } }      // .
        };
        
        for (u32 c = 0; c < 8; ++c)
        {
            vec3f cp = pos + corner[c] * 0.5f;
            vec3f cv = corner[c];
            vec3f cpc = cp - cv * 0.125f;

            // remove corner covered by 3 neighbours
            if (cn[c][0] && cn[c][1] && cn[c][2])
                continue;

            // if we have no neighbours on 3 sides we are a corner
            if (!cn[c][0] && !cn[c][1] && !cn[c][2])
            {
                u32 corner = get_new_node(scene);
                scene->geometry_names[corner] = file_top_corner;
                scene->transforms[corner].translation = cpc;
                scene->transforms[corner].rotation = corner_rotation[c];
                scene->transforms[corner].scale = vec3f::one();
                scene->entities[corner] |= CMP_TRANSFORM;

                continue;
            }

            // middle edge
            if (!cn[c][0] && !cn[c][2] && cn[c][1])
            {
                u32 tile = get_new_node(scene);
                scene->geometry_names[tile] = file_middle_corner;
                scene->transforms[tile].translation = cpc;
                scene->transforms[tile].rotation = corner_rotation[c];
                scene->transforms[tile].scale = vec3f::one();
                scene->entities[tile] |= CMP_TRANSFORM;

                continue;
            }

            // top edge
            if (!cn[c][1])
            {
                if (!cn[c][0])
                {
                    u32 tile = get_new_node(scene);
                    scene->geometry_names[tile] = file_top_side;
                    scene->transforms[tile].translation = cpc;
                    scene->transforms[tile].rotation = quat(0.0f, 0.0f, 0.0f);
                    scene->transforms[tile].scale = vec3f::one();
                    
                    if(cv.x < 0.0f)
                        scene->transforms[tile].rotation *= quat(0.0f, r180, 0.0f);
                    
                    if(cv.y < 0.0f)
                        scene->transforms[tile].rotation *= quat(-r90, 0.0f, 0.0f);
                    
                    scene->entities[tile] |= CMP_TRANSFORM;

                    continue;
                }
                else if (!cn[c][2])
                {
                    u32 tile = get_new_node(scene);
                    scene->geometry_names[tile] = file_top_side;
                    scene->transforms[tile].translation = cpc;
                    scene->transforms[tile].rotation = quat(0.0f, -r90, 0.0f);
                    scene->transforms[tile].scale = vec3f::one();
                    
                    if(cv.z < 0.0f)
                        scene->transforms[tile].rotation *= quat(0.0f, r180, 0.0f);
                    
                    if(cv.y < 0.0f)
                        scene->transforms[tile].rotation *= quat( -r90, 0.0f, 0.0f);
                    
                    scene->entities[tile] |= CMP_TRANSFORM;

                    continue;
                }
            }

            // plain face..
            
            // rotations can be in 3 positions depending on neighbors
            u32 rot_i = 0;
            
            Str model = file_middle_side;
            
            if(!cn[c][1])
            {
                model = file_top_centre;
                rot_i = 1;
            }
            else if(!cn[c][2])
            {
                rot_i = 2;
            }

            u32 tile = get_new_node(scene);
            scene->geometry_names[tile] = model;
            scene->transforms[tile].translation = cpc;
            scene->transforms[tile].rotation = corner_face_rotations[c][rot_i];
            scene->transforms[tile].scale = vec3f::one();
            scene->entities[tile] |= CMP_TRANSFORM;
        }

        // 12 edge tiles
        static vec3f edges[] = {
            vec3f(-1.0f, 1.0f,  0.0f), //-x
            vec3f( 0.0f, 1.0f, -1.0f), //-z
            vec3f( 1.0f, 1.0f,  0.0f), //+x
            vec3f( 0.0f, 1.0f,  1.0f), //+z
            vec3f(-1.0f, -1.0f, 0.0f), //-x -y
            vec3f(0.0f, -1.0f, -1.0f), //-z -y
            vec3f(1.0f, -1.0f,  0.0f), //+x -y
            vec3f(0.0f, -1.0f,  1.0f), //+z -y
            vec3f(1.0f, 0.0f, 1.0f),   //+x+z mid
            vec3f(-1.0f, 0.0f, 1.0f),  //-x+z mid
            vec3f(-1.0f, 0.0f, -1.0f), //-x-z mid
            vec3f(1.0f, 0.0f, -1.0f)   //+x-z mid
        };
        
        //edge tangent
        static vec3f et[] = {
            vec3f( 0.0f, 0.0f, 1.0f),
            vec3f( 1.0f, 0.0f, 0.0f),
            vec3f( 0.0f, 0.0f, 1.0f),
            vec3f( 1.0f, 0.0f, 0.0f),
            vec3f( 0.0f, 0.0f, 1.0f),
            vec3f( 1.0f, 0.0f, 0.0f),
            vec3f( 0.0f, 0.0f, 1.0f),
            vec3f( 1.0f, 0.0f, 0.0f),
            vec3f( 0.0f, 1.0f, 0.0f),
            vec3f( 0.0f, 1.0f, 0.0f),
            vec3f( 0.0f, 1.0f, 0.0f),
            vec3f( 0.0f, 1.0f, 0.0f),
        };

        bool en[][2] = {
            { neighbour[3], neighbour[1] },
            { neighbour[5], neighbour[1] },
            { neighbour[0], neighbour[1] },
            { neighbour[2], neighbour[1] },
            { neighbour[3], neighbour[4] },
            { neighbour[5], neighbour[4] },
            { neighbour[0], neighbour[4] },
            { neighbour[2], neighbour[4] },
            { neighbour[2], neighbour[0] },
            { neighbour[3], neighbour[2] },
            { neighbour[3], neighbour[5] },
            { neighbour[5], neighbour[0] }
        };
        
        static quat edge_rotation[] = {
            quat(0.0f, r180, 0.0f),
            quat(0.0f, r90, 0.0f),
            quat(0.0f, 0.0f, 0.0f),
            quat(0.0f, -r90, 0.0f),
            quat(-r90, 0.0f, -r180),
            quat(0.0f,  r90, 0.0f) * quat(-r90, 0.0f, 0.0f),
            quat(-r90, 0.0f, 0.0f),
            quat(0.0f, -r90, 0.0f) * quat(-r90, 0.0f, 0.0f),
            quat(0.0f, 0.0f, 0.0f),
            quat(0.0f, -r90, 0.0f),
            quat(0.0f, r180, 0.0f),
            quat(0.0f, r90, 0.0f)
        };
        
        // side edge rotations can be in 2 positions
        static quat side_edge_rotation[] = {
            quat(0.0f, 0.0f, 0.0f),
            quat(0.0f, -r90, 0.0f),
            quat(0.0f, r90, 0.0f),
            quat(0.0f, 0.0f, 0.0f),
        };
        
        static quat side_edge_rotation_2[] = {
            quat(0.0f, -r90, 0.0f),
            quat(0.0f, -r180, 0.0f),
            quat(0.0f, r180, 0.0f),
            quat(0.0f, r90, 0.0f),
        };
        
        static quat bottom_edge_rotation[] = {
            quat(0.0f, r180, 0.0f),
            quat(0.0f, r90, 0.0f),
            quat(0.0f, 0.0f, 0.0f),
            quat(0.0f, -r90, 0.0f),
        };

        for (u32 e = 0; e < 12; ++e)
        {
            if(en[e][0] && en[e][1])
                continue;
            
            vec3f ep = pos + edges[e] * 0.5f;
            vec3f ev = edges[e];
            vec3f epc = ep - ev * 0.125f;
            
            vec3f esubp[] = {
                epc + et[e] * 0.125f,
                epc + et[e] * -0.125f
            };
            
            Str model = "";
            Str file = "";
            
            quat rot_r = edge_rotation[e];
            
            if (!en[e][0] && !en[e][1])
            {
                // corners / edges
                model = file_top_side;
                
                if(e >= 8) model = file_middle_corner;
                
                if(e >= 8)
                {
                    model = file_middle_corner;
                }
            }
            else if (en[e][0] && e < 8)
            {
                if(e < 4)
                {
                    // top faces
                    model = file_top_centre;
                }
                else
                {
                    // bottom faces.. todo. use basic_top_center?
                    model = file_middle_side;
                }
            }
            else if(en[e][0] || en[e][1])
            {
                // side yup faces
                model = file_middle_side;
                
                // here we need lookups for diff rotations because the models are not uniform
                if(e >= 8)
                {
                    if(en[e][1])
                    {
                        rot_r = side_edge_rotation_2[e%4];
                    }
                    else
                    {
                        rot_r = side_edge_rotation[e%4];
                    }
                }
                else if(e >= 4)
                {
                    // bottom edge mid faces
                    rot_r = bottom_edge_rotation[e%4];
                }
            }
            
            if(!model.empty())
            {
                for(u32 i = 0; i < 2; ++i)
                {
                    u32 tile = get_new_node(scene);
                    scene->geometry_names[tile] = model;
                    scene->transforms[tile].translation = esubp[i];
                    scene->transforms[tile].rotation = rot_r;
                    scene->transforms[tile].scale = vec3f::one();
                    scene->entities[tile] |= CMP_TRANSFORM;
                }
            }
        }

        // face tiles
        static quat face_rotation[] = {
            quat(-r90, 0.0f, 0.0f),
            quat(0.0f, 0.0f, 0.0f),
            quat(0.0f, 0.0f, r90),
            quat(r90, 0.0f, 0.0f),
            quat(r180, 0.0f, 0.0f),
            quat(0.0f, 0.0f, -r90)
        };
        
        for (u32 f = 0; f < 6; ++f)
        {
            vec3f fp = pos + np[f] * 0.5f;
            vec3f fv = np[f];
            vec3f fpc = fp + fv * -0.125f;
            
            vec3f u = nt[f];
            vec3f v = nbt[f];
            
            vec3f fpsub[] = {
                fpc + (-u - v) * 0.125f,
                fpc + (-u + v) * 0.125f,
                fpc + (u + v) * 0.125f,
                fpc + (u - v) * 0.125f
            };

            if (neighbour[f])
                continue;
            
            // add 4
            for(u32 i = 0; i < 4; ++i)
            {
                u32 tile = get_new_node(scene);
                scene->geometry_names[tile] = file_top_centre;
                scene->transforms[tile].translation = fpsub[i];
                scene->transforms[tile].rotation = face_rotation[f];
                scene->transforms[tile].scale = vec3f::one();
                scene->entities[tile] |= CMP_TRANSFORM;
            }

            put::dbg::add_point(fp, 0.5f);
        }
    }
    
    // bake vertex buffer
    u32 end = scene->num_nodes;

    u32* node_list = nullptr;
    for(u32 i = start; i < end; ++i)
    {
        scene->parents[i] = parent;
        scene->bounding_volumes[i].min_extents = vec3f(-0.125f);
        scene->bounding_volumes[i].max_extents = vec3f( 0.125f);
        
        sb_push(node_list, i);
    }

    // need to bake world mats and extents
    update_scene(scene, 0.0f);
    
    // bake vertex buffer
    bake_nodes_to_vb(scene, parent, node_list);
}

void setup_character(put::ecs::ecs_scene* scene)
{
    // load main model
    dr.root = load_pmm("data/models/characters/doctor/Doctor.pmm", scene);
    
    // load anims
    anim_handle idle = load_pma("data/models/characters/doctor/anims/doctor_idle01.pma");
    anim_handle walk = load_pma("data/models/characters/doctor/anims/doctor_walk.pma");
    anim_handle run = load_pma("data/models/characters/doctor/anims/doctor_run.pma");
    anim_handle jump = load_pma("data/models/characters/doctor/anims/doctor_idle_jump.pma");
    anim_handle run_l = load_pma("data/models/characters/doctor/anims/doctor_run_l.pma");
    anim_handle run_r = load_pma("data/models/characters/doctor/anims/doctor_run_r.pma");
    
    // bind to rig
    bind_animation_to_rig(scene, idle, dr.root);
    bind_animation_to_rig(scene, walk, dr.root);
    bind_animation_to_rig(scene, run, dr.root);
    bind_animation_to_rig(scene, jump, dr.root);
    bind_animation_to_rig(scene, run_l, dr.root);
    bind_animation_to_rig(scene, run_r, dr.root);

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
    load_scene("data/scene/basic_level.pms", scene, true);
    //load_scene("data/scene/basic_level-2.pms", scene, true);
    //load_scene("data/scene/basic_level-3.pms", scene, true);
    //load_scene("data/scene/basic_level-4.pms", scene, true);
    //load_scene("data/scene/basic_level-ex.pms", scene, true);
    
    // todo move to level editor
    load_pmm("data/models/environments/general/basic_top_corner.pmm", scene, PMM_GEOMETRY | PMM_MATERIAL);
    load_pmm("data/models/environments/general/basic_middle_corner.pmm", scene, PMM_GEOMETRY | PMM_MATERIAL);
    load_pmm("data/models/environments/general/basic_top_side.pmm", scene, PMM_GEOMETRY | PMM_MATERIAL);
    load_pmm("data/models/environments/general/basic_middle_side.pmm", scene, PMM_GEOMETRY | PMM_MATERIAL);
    load_pmm("data/models/environments/general/basic_top_center.pmm", scene, PMM_GEOMETRY | PMM_MATERIAL);
    
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

static bool press_debounce(u32 key, bool& db)
{
    if(pen::input_key(key))
    {
        if(!db)
        {
            db = true;
            return true;
        }
    }
    else
    {
        db = false;
    }
    
    return false;
}

void update_level_editor(ecs_controller& ecsc, ecs_scene* scene, f32 dt)
{
    ecs::editor_enable(true);
    
    camera* camera = ecsc.camera;
    dr_ecs_exts* ext = (dr_ecs_exts*)ecsc.context;

    static bool open = false;
    static vec3i slice = vec3i(0, 0, 0);
    static vec3f pN = vec3f::unit_y();
    static vec3f p0 = vec3f::zero();
    
    ImGui::BeginMainMenuBar();
    if(ImGui::MenuItem(ICON_FA_BRIEFCASE))
        open = !open;
    ImGui::EndMainMenuBar();
    
    if(!open)
        return;
    
    ecs::editor_enable(false);
    
    ImGui::Begin("Toolbox");
    
    if (ImGui::Button("Bake"))
    {
        bake_tile_blocks(scene, ext);
    }

    ImGui::InputInt("Level", &slice[1]);
    p0.y = slice.y;
    
    static bool _dbu = false;
    if(press_debounce(PK_1, _dbu))
        slice[1]++;
    
    static bool _dbd = false;
    if(press_debounce(PK_2, _dbd))
        slice[1]--;

    ImGui::End();
    
    if(!can_edit())
        return;
    
    pen::mouse_state ms = pen::input_get_mouse_state();
    
    // get a camera ray
    vec2i vpi = vec2i(pen_window.width, pen_window.height);
    ms.y = vpi.y - ms.y;
    mat4  view_proj = camera->proj * camera->view;
    vec3f r0 = maths::unproject_sc(vec3f(ms.x, ms.y, 0.0f), view_proj, vpi);
    vec3f r1 = maths::unproject_sc(vec3f(ms.x, ms.y, 1.0f), view_proj, vpi);
    vec3f rv = normalised(r1 - r0);
    
    // intersect with edit plane
    vec3f ip = maths::ray_plane_intersect(r0, rv, p0, pN);
    
    // snap to grid
    ip = floor(ip) + vec3f(0.5f);
    ip.y = slice.y-0.5f;
    
    // tile
    dbg::add_aabb(ip - vec3f(0.5f), ip + vec3f(0.5f), vec4f::white());
    
    // focus
    static bool _dbf;
    if(press_debounce(PK_F, _dbf))
        ecsc.camera->focus = ip;
    
    // detect current occupancy
    cast_result cast;
    bool occupied = false;
    
    physics::ray_cast_params rcp;
    rcp.start = ip + vec3f(0.0f, 1.0f, 0.0f);
    rcp.end = ip;
    rcp.callback = &tilemap_ray_cast;
    rcp.user_data = &cast;
    rcp.group = 1;
    rcp.mask = 1;
    
    physics::cast_ray(rcp, true);
    
    occupied = cast.set;
    
    if(occupied)
        return;
    
    if(ms.buttons[PEN_MOUSE_L])
    {
        add_tile_block(scene, ext, ip);
    }
    
    // detect neighbours for guides
    vec3f np[6] = {
        vec3f::unit_x(),
        vec3f::unit_y(),
        vec3f::unit_z(),
        -vec3f::unit_x(),
        -vec3f::unit_y(),
        -vec3f::unit_z()
    };
    
    for(u32 n = 0; n < 6; ++n)
    {
        cast.set = false;
        
        vec3f nip = ip + np[n] * 100000.0f;
        
        physics::ray_cast_params rcp;
        rcp.start = ip;
        rcp.end = nip;
        rcp.callback = &tilemap_ray_cast;
        rcp.user_data = &cast;
        rcp.group = 1;
        rcp.mask = 1;
        
        physics::cast_ray(rcp, true);
        
        // ..
        if(cast.set)
        {
            dbg::add_point(cast.pos, 0.1f, vec4f::yellow());
            
            vec3f perp = cast.normal;
            
            for(u32 i = 0; i < 4; ++i)
            {
                dbg::add_line(cast.pos, cast.pos + cast.normal);
            }
        }
        
        //dbg::add_aabb(nip - vec3f(0.5f), nip + vec3f(0.5f), vec4f::red());
        //dbg::add_line(ip, nip, vec4f::green());
    }
    
}

void get_controller_input(camera* cam, controller_input& ci)
{
    // clear state
    ci.actions = 0;
    
    vec3f left_stick = vec3f::zero();
    vec2f right_stick = vec2f::zero();
    
    vec3f xz_dir = cam->focus - cam->pos;
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
}

void sccb(const physics::sphere_cast_result& result)
{
    cast_result* cc = (cast_result*)result.user_data;
    
    cc->pos = result.point;
    cc->normal = result.normal;

    if(result.physics_handle != PEN_INVALID_HANDLE)
        cc->set = true;
}

void rccb(const physics::ray_cast_result& result)
{
    cast_result* cc = (cast_result*)result.user_data;

    cc->pos = result.point;
    cc->normal = result.normal;

    if (result.physics_handle != PEN_INVALID_HANDLE)
        cc->set = true;
}

pen::multi_buffer<physics::contact*, 2> contacts;
void ctcb(const physics::contact_test_results& results)
{
    static bool init = true;
    if(init)
    {
        init = false;
        contacts._data[contacts._bb] = nullptr;
    }
    
    if(contacts._data[contacts._bb])
        sb_free(contacts._data[contacts._bb]);
    
    contacts._data[contacts._bb] = results.contacts;
    
    contacts.swap_buffers();
}

#define assert_nan(V) for(u32 i = 0; i < 3; ++i) \
                            PEN_ASSERT(!std::isnan(V[i]))

void update_character_controller(ecs_controller& ecsc, ecs_scene* scene, f32 dt)
{
    put::camera* camera = ecsc.camera;

    dt = min(dt, 1.0f / 10.0f);

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

    static u32 hp = 0;
    static f32 pos_hist_x[60];
    static f32 pos_hist_y[60];
    static f32 pos_hist_z[60];

    // controller ----------------------------------------------------------------------------------------------------------
    
    get_controller_input(camera, ci);

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

    if (ci.movement_vel > 0.33)
    {
        ci.dir_angle = atan2(ci.movement_dir.x, ci.movement_dir.z);
    }

    // update state --------------------------------------------------------------------------------------------------------
    
    cmp_anim_controller_v2& controller = scene->anim_controller_v2[dr.root];
    
    pc.acc = vec3f(0.0f, -gravity_strength, 0.0f);

    if(pc.loco_vel == 0.0f)
    {
        // idle state
        controller.blend.anim_a = dr.anim_idle;
        controller.blend.anim_b = dr.anim_walk;
        
        //controller.anim_instances[dr.anim_walk].time = 0.0f;
        //controller.anim_instances[dr.anim_run].time = 0.0f;
        
        f32 mid = controller.anim_instances[dr.anim_run].length / 2.0f;
        
        controller.anim_instances[dr.anim_walk].time = controller.anim_instances[dr.anim_walk].length / 2.0f;
        
        controller.anim_instances[dr.anim_run].time = 0.0f;
        //controller.anim_instances[dr.anim_run].root_translation = pc.pos;
        controller.anim_instances[dr.anim_run].root_delta = vec3f::zero();
        controller.anim_instances[dr.anim_run].flags |= anim_flags::PAUSED;
        controller.anim_instances[dr.anim_run].flags |= anim_flags::LOOPED;
    }
    else
    {
        controller.anim_instances[dr.anim_run].flags &= ~anim_flags::PAUSED;
        
        // locomotion state
        controller.blend.anim_a = dr.anim_walk;
        controller.blend.anim_b = dr.anim_run;

        controller.blend.ratio = smooth_step(pc.loco_vel, 0.0f, 5.0f, 0.0f, 1.0f);

        if (controller.blend.ratio >= 1.0)
            controller.blend.anim_a = dr.anim_run;
        else if (controller.blend.ratio <= 0.0)
            controller.blend.anim_a = dr.anim_walk;
        
        controller.blend.anim_a = dr.anim_run;
        controller.blend.anim_b = dr.anim_run;
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
        
        pc.air = max(pc.air, dt);
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

    // update pos from anim
    pc.pps = pc.pos;
    pc.pos = scene->transforms[dr.root].translation;

    if (pc.air == 0.0f)
    {
        vec3f xzdir = (pc.pos - pc.pps) * vec3f(1.0f, 0.0f, 1.0f);

        // transform loco.. todo use root motion vector
        f32 vel2 = mag2(xzdir);
        if (vel2 > 0)
        {
            xzdir = normalised(xzdir);

            vec3f cp1 = cross(pc.surface_normal, xzdir);
            vec3f perp = cross(cp1, pc.surface_normal);

            f32 vel = mag(pc.pos - pc.pps);
            pc.pos = pc.pps + perp * vel;
        }
    }

    // euler integration
    f32 dtt = dt / (1.0f / 60.0f);
    f32 ar = pow(0.8f, dtt);
    
    if (!(scene->flags & PAUSE_UPDATE))
    {
        pc.vel *= vec3f(ar, 1.0f, ar);
        pc.vel += pc.acc * dt;
        pc.pos += pc.vel * dt;
    }

    // resolve collisions ---------------------------------------------------------------------------------------------------

    vec3f mid = pc.pos + vec3f(0.0f, 0.5f, 0.0f);       // mid pos
    vec3f head = pc.pos + vec3f(0.0f, 0.65f, 0.0f);     // position of head sphere
    vec3f feet = pc.pps + vec3f(0.0f, 0.35f, 0.0f);     // position of lower sphere

    // casts
    cast_result wall_cast;
    cast_result surface_cast;
        
    physics::sphere_cast_params scp;
    scp.from = mid - ci.movement_dir * 0.3f;
    scp.to = mid + ci.movement_dir * 1000.0f;
    scp.dimension = vec3f(0.3f);
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
    if (mag(cv) < 0.3f && wall_cast.set)
    {
        f32 diff = 0.3f - mag(cv);
        pc.pos += wall_cast.normal * diff;
    }
    
    // floor collision from casts
    f32 cvm2 = mag(mid - surface_cast.pos);
    f32 dp = dot(surface_cast.normal, vec3f::unit_y());

    // todo.. resolve magic numbers
    if (cvm2 <= 0.51f && dp > 0.7f && pc.vel.y <= 0.0f && surface_cast.set)
    {
        pc.air = 0.0f;
        f32 diff = 0.5f - cvm2;
        pc.pos += vec3f::unit_y() * diff;
        pc.vel.y = 0.0f;

        // normal and perp for transforming locomotion
        pc.surface_normal = surface_cast.normal;
        vec3f cp1 = cross(pc.surface_normal, ci.movement_dir);
        pc.surface_perp = cross(cp1, pc.surface_normal);
    }
    else
    {
        // in air
        pc.air += dt;
    }

    // get contacts.. 1 frame behhind because of async physics
    physics::contact_test_params ctp;

    ctp.entity = scene->physics_handles[dr.root];
    ctp.callback = ctcb;

    physics::contact_test(ctp);
    
    // resolve overlaps
    const physics::contact* cb = contacts.frontbuffer();
    u32 num_contacts = sb_count(cb);
    for(u32 i = 0; i < num_contacts; ++i)
    {
        const vec3f& p = cb[i].pos;
        const vec3f& n = cb[i].normal;

        vec3f pxz = vec3f(p.x, 0.0f, p.z);
        vec3f qxz = vec3f(pc.pos.x, 0.0f, pc.pos.z);
        f32 d = dot(cb[i].normal, vec3f::unit_y());

        // walls
        f32 m = mag(pxz - qxz);
        if(m < capsule_radius)
        {
            if(abs(d) < 0.7f)
            {
                f32 diff = capsule_radius - m;
                pc.pos += cb[i].normal * diff;
            }
            else if(n.y > 0.0f && cvm2 > 0.6f)
            {
                // floor edges
                f32 dc = 1.0 - abs(pc.pos.y + 0.35f - p.y); // distance to cylinder part of the capsule
                f32 rad = dc * 0.3f;
                f32 diff = rad - m;

                vec3f vn = cb[i].normal * vec3f(1.0f, 0.0f, 1.0f);
                if(mag2(vn) != 0.0f)
                    pc.pos += normalised(vn) * diff;
            }
        }

        //ceils
        vec3f top = pc.pos + vec3f(0.0f, 0.65f, 0.0f);
        m = mag(top - p);
        if (m < capsule_radius && p.y > top.y)
        {
            // ceil
            f32 diff = capsule_radius - m;
            pc.pos += normalised(cb[i].normal) * diff;
            pc.vel.y *= 0.5f;
        }
    }

    // set onto entity
    if (!(scene->flags & PAUSE_UPDATE))
    {
        scene->initial_transform[5].rotation = quat(0.0f, ci.dir_angle, 0.0f);
        scene->transforms[dr.root].translation = pc.pos;
        scene->entities[dr.root] |= CMP_TRANSFORM;
    }

    // camera ---------------------------------------------------------------------------------------------------------------

    if( game_cam )
    {
        f32 zl = smooth_step(pc.loco_vel, 0.0f, 5.0f, 0.0f, 1.0f);

        pc.cam_y_target = lerp(pc.cam_y_target, pc.pos.y, camera_lerp_y * dt);
        pc.cam_pos_target = vec3f(pc.pos.x, pc.cam_y_target, pc.pos.z);
        pc.cam_zoom_target = lerp(min_zoom, max_zoom, zl);

        camera->focus = lerp(camera->focus, pc.cam_pos_target, camera_lerp * dt);
        camera->zoom = lerp(camera->zoom, pc.cam_zoom_target, camera_lerp * dt);

        if(mag(ci.cam_rot) > 0.2f)
            camera->rot += (ci.cam_rot * fabs(ci.cam_rot)) * dt;

        static f32 ulimit = -(M_PI / 2.0f) - M_PI * 0.02f;
        static f32 llimit = -M_PI * 0.02f;
        camera->rot.x = min(max(camera->rot.x, ulimit), llimit);
    }
    
    // debug crap -----------------------------------------------------------------------------------------------------------
    
    if (pen::input_key(PK_O))
        pc.vel = vec3f::zero();
    
    if(debug_lines)
    {
        put::dbg::add_point(surface_cast.pos, 0.1f, vec4f::green());
        put::dbg::add_line(surface_cast.pos, surface_cast.pos + pc.surface_normal, vec4f::cyan());

        vec3f smid = surface_cast.pos + vec3f(0.0f, 0.5f, 0.0f);
        put::dbg::add_line(smid, smid + pc.surface_perp, vec4f::red());

        put::dbg::add_circle(vec3f::unit_y(), mid, 0.3f, vec4f::green());
        put::dbg::add_point(wall_cast.pos, 0.1f, vec4f::green());

        put::dbg::add_point(head, 0.3f, vec4f::magenta());
        put::dbg::add_point(feet, 0.3f, vec4f::magenta());
               
        vec3f xz_dir = camera->focus - camera->pos;
        xz_dir.y = 0.0f;
        xz_dir = normalised(xz_dir);

        put::dbg::add_line(mid, mid + xz_dir, vec4f::white());
    }
    
    static bool open = false;
    ImGui::BeginMainMenuBar();
    if(ImGui::MenuItem(ICON_FA_MALE))
        open = !open;
    ImGui::EndMainMenuBar();
    
    if(!open)
        return;
    
    pos_hist_x[hp] = pc.pos.x - pc.pps.x;
    pos_hist_y[hp] = pc.pos.y - pc.pps.y;
    pos_hist_z[hp] = pc.pos.z - pc.pps.z;
    
    static f32 scale_min = -0.3f;
    static f32 scale_max = 0.3f;
    
    ImGui::PlotLines("X", &pos_hist_x[0], 60, hp, "", scale_min, scale_max);
    ImGui::PlotLines("Y", &pos_hist_y[0], 60, hp, "", scale_min, scale_max);
    ImGui::PlotLines("z", &pos_hist_z[0], 60, hp, "", scale_min, scale_max);
    
    hp = (hp + 1) % 60;
    
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
    ImGui::InputFloat2("cam.rot", &camera->rot[0]);
}

void update_game_controller(ecs_controller& ecsc, ecs_scene* scene, f32 dt)
{
    update_character_controller(ecsc, scene, dt);
    update_level_editor(ecsc, scene, dt);
}

void update_game_components(ecs_extension& extension, ecs_scene* scene, f32 dt)
{
    dr_ecs_exts* ext = (dr_ecs_exts*)extension.context;
    
    for(u32 n = 0; n < scene->num_nodes; ++n)
    {
        if(ext->cmp_flags[n] & CMP_CUSTOM_ANIM)
        {
            scene->transforms[n].rotation *= quat(0.0f, dt, 0.0f);
            scene->entities[n] |= CMP_TRANSFORM;
        }
    }
}

PEN_TRV pen::user_entry( void* params )
{
    //unpack the params passed to the thread and signal to the engine it ok to proceed
    pen::job_thread_params* job_params = (pen::job_thread_params*)params;
    pen::job* p_thread_info = job_params->job_info;
    pen::semaphore_post(p_thread_info->p_sem_continue, 1);
    
    pen::jobs_create_job( physics::physics_thread_main, 1024*10, nullptr, pen::THREAD_START_DETACHED );
    
	dev_ui::init();
	dbg::init();
    
	//create main camera and controller
	put::camera main_camera;
	put::camera_create_perspective( &main_camera, 60.0f, k_use_window_aspect, 0.1f, 1000.0f );

    //create the main scene and controller
    ecs_scene* main_scene = create_scene("main_scene");
    editor_init( main_scene, &main_camera );

    // create and register game specific extensions
    dr_ecs_exts* exts = (dr_ecs_exts*)dr_ecs_extension(main_scene);
    
    // controllers
    ecs::ecs_controller game_controller;
    game_controller.camera = &main_camera;
    game_controller.update_func = &update_game_controller;
    game_controller.name = "dr_scientist_game_controller";
    game_controller.context = exts;
    game_controller.id_name = PEN_HASH(game_controller.name.c_str());

    ecs::register_ecs_controller(main_scene, game_controller);
    
    //create view renderers
    put::scene_view_renderer svr_main;
    svr_main.name = "ces_render_scene";
    svr_main.id_name = PEN_HASH(svr_main.name.c_str());
    svr_main.render_function = &render_scene_view;
    
    put::scene_view_renderer svr_editor;
    svr_editor.name = "ces_render_editor";
    svr_editor.id_name = PEN_HASH(svr_editor.name.c_str());
    svr_editor.render_function = &render_scene_editor;
    
    pmfx::register_scene_view_renderer(svr_main);
    pmfx::register_scene_view_renderer(svr_editor);
    pmfx::register_scene(main_scene, "main_scene");
    pmfx::register_camera(&main_camera, "model_viewer_camera");

    pmfx::init("data/configs/editor_renderer.jsn");
    
    setup_character(main_scene);
    
    while( 1 )
    {
        static u32 frame_timer = pen::timer_create("user_thread");
        pen::timer_start(frame_timer);

		put::dev_ui::new_frame();
        
        //pmfx::update();
        
        ecs::update();
        
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
