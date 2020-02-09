#include "dr_scientist.h"
#include "../shader_structs/forward_render.h"

// collecting mushroom

// animation frame setting
// animation tracking (attack)

// min jump height

using physics::cast_result;

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
    
    // unit cube axis
    static vec3f axis[6] = {
        vec3f::unit_x(),
        vec3f::unit_y(),
        vec3f::unit_z(),
        -vec3f::unit_x(),
        -vec3f::unit_y(),
        -vec3f::unit_z()
    };
    
    // axis tangent
    static vec3f axis_t[6] = {
        vec3f::unit_y(),
        vec3f::unit_x(),
        vec3f::unit_x(),
        vec3f::unit_y(),
        vec3f::unit_x(),
        vec3f::unit_x()
    };
    
    // axis bi tangent
    static vec3f axis_bt[6] = {
        -vec3f::unit_z(),
        vec3f::unit_z(),
        vec3f::unit_y(),
        vec3f::unit_z(),
        -vec3f::unit_z(),
        -vec3f::unit_y()
    };
}

void collision_mesh_from_vertex_list(f32* verts, u32 num, physics::collision_mesh_data& cmd)
{
    // indices are just 1 to 1 with verts
    u32 num_verts = num;
    u32 vb_size = num_verts*3*sizeof(f32);
    u32 ib_size = num_verts*sizeof(u32);
    
    cmd.vertices = (f32*)pen::memory_alloc(vb_size);
    memcpy(cmd.vertices, verts, vb_size);
    
    cmd.indices = (u32*)pen::memory_alloc(ib_size);
    for(u32 i = 0; i < num_verts; ++i)
    {
        cmd.indices[i] = i;
    }
    
    cmd.num_floats = num_verts * 3;
    cmd.num_indices = num_verts;
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
            ImGui::Value("Mask", ext->tile_blocks[si].neighbour_mask);
            ImGui::Value("Island", ext->tile_blocks[si].island_id);
        }
    }
}

void dr_ecs_extension_shutdown(ecs_extension& extension)
{
    delete (dr_ecs_exts*)extension.context;
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
    ext.shutdown = &dr_ecs_extension_shutdown;

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
    static u32 p = get_new_entity(scene);
    scene->transforms[p].translation = vec3f::zero();
    scene->transforms[p].rotation = quat();
    scene->transforms[p].scale = vec3f(1.0f);
    scene->entities[p] |= e_cmp::transform;
    scene->names[p] = "basic_level";

    static material_resource* default_material = get_material_resource(PEN_HASH("default_material"));
    static geometry_resource* box = get_geometry_resource(PEN_HASH("cube"));
    
    u32 b = get_new_entity(scene);
    scene->names[b] = "ground";
    scene->transforms[b].translation = pos;
    scene->transforms[b].rotation = quat();
    scene->transforms[b].scale = vec3f(0.5f);
    scene->entities[b] |= e_cmp::transform;
    scene->parents[b] = p;
    scene->physics_data[b].rigid_body.shape = physics::e_shape::box;
    scene->physics_data[b].rigid_body.mass = 0.0f;
    scene->physics_data[b].rigid_body.group = 1;
    scene->physics_data[b].rigid_body.mask = 0xffffffff;

    // ext flags
    ext->cmp_flags[b] |= e_game_cmp::tile_block;

    instantiate_geometry(box, scene, b);
    instantiate_material(default_material, scene, b);
    instantiate_model_cbuffer(scene, b);
    
    pen::renderer_consume_cmd_buffer();
    physics::physics_consume_command_buffer();
    
    instantiate_rigid_body(scene, b);
}

void detect_neighbours(vec3f p, f32 tile_size, u32 neighbours[6])
{
    for(u32 n = 0; n < 6; ++n)
    {
        neighbours[n] = PEN_INVALID_HANDLE;
                
        vec3f nip = p + axis[n] * tile_size;
        
        physics::ray_cast_params rcp;
        rcp.start = p;
        rcp.end = nip;
        rcp.group = 1;
        rcp.mask = 1;
        
        cast_result cast = physics::cast_ray_immediate(rcp);
        
        if(cast.set)
        {
            neighbours[n] = cast.physics_handle;
        }
    }
}

void detect_neighbours_ex(vec3f p, f32 tile_size, u32 neighbours[6], ecs_scene* scene, dr_ecs_exts* ext)
{
    detect_neighbours(p, tile_size, neighbours);
    
    // for non neighbours check inners
    for(u32 n = 0; n < 6; ++n)
    {
        if(neighbours[n] != PEN_INVALID_HANDLE)
            continue;
        
        for(u32 e = 0; e < scene->num_entities; ++e)
        {
            if(!(ext->cmp_flags[e] & e_game_cmp::tile_block))
                continue;
            
            if(!(ext->tile_blocks[e].flags & e_tile_flags::inner))
                continue;
            
            vec3f pp = scene->transforms[e].translation;
            if(maths::point_inside_sphere(p + axis[n], 1.0f, pp))
            {
                neighbours[n] = e;
                break;
            }
        }
    }
}

void bake_tile_blocks(put::ecs::ecs_scene* scene, dr_ecs_exts* ext, u32* entities, u32 num_entities, u32 parent)
{
    Str file_top_corner = "data/models/environments/general/basic_top_corner.pmm";
    Str file_middle_corner = "data/models/environments/general/basic_middle_corner.pmm";
    Str file_top_side = "data/models/environments/general/basic_top_side.pmm";
    Str file_middle_side = "data/models/environments/general/basic_middle_side.pmm";
    Str file_top_centre = "data/models/environments/general/basic_top_center.pmm";
    
    static const f32 r90 = M_PI/2.0f;
    static const f32 r180 = M_PI;
    
    u32* block_entities = nullptr;
    
    for(u32 i = 0; i < num_entities; ++i)
    {
        u32 n = entities[i];
        
        if (!(ext->cmp_flags[n] & e_game_cmp::tile_block))
            continue;
        
        if(ext->tile_blocks[n].flags & e_tile_flags::inner)
            continue;

        scene->state_flags[n] |= e_state::hidden;
        
        u32 neigbour_i[6];
        
        vec3f pos = scene->transforms[n].translation;
        
        detect_neighbours_ex(pos, 1.0, neigbour_i, scene, ext);
        
        bool neighbour[6] = { 0 };
        
        u32 nc = 0;
        
        for(u32 i = 0; i < 6; ++i)
        {
            if(neigbour_i[i] != PEN_INVALID_HANDLE)
            {
                nc++;
                neighbour[i] = true;
            }
        }
        
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
                u32 corner = get_new_entity(scene);
                scene->geometry_names[corner] = file_top_corner;
                scene->transforms[corner].translation = cpc;
                scene->transforms[corner].rotation = corner_rotation[c];
                scene->transforms[corner].scale = vec3f::one();
                scene->entities[corner] |= e_cmp::transform;
                sb_push(block_entities, corner);
                
                continue;
            }

            // middle edge
            if (!cn[c][0] && !cn[c][2] && cn[c][1])
            {
                u32 tile = get_new_entity(scene);
                scene->geometry_names[tile] = file_middle_corner;
                scene->transforms[tile].translation = cpc;
                scene->transforms[tile].rotation = corner_rotation[c];
                scene->transforms[tile].scale = vec3f::one();
                scene->entities[tile] |= e_cmp::transform;
                sb_push(block_entities, tile);

                continue;
            }

            // top edge
            if (!cn[c][1])
            {
                if (!cn[c][0])
                {
                    u32 tile = get_new_entity(scene);
                    scene->geometry_names[tile] = file_top_side;
                    scene->transforms[tile].translation = cpc;
                    scene->transforms[tile].rotation = quat(0.0f, 0.0f, 0.0f);
                    scene->transforms[tile].scale = vec3f::one();
                    
                    if(cv.x < 0.0f)
                        scene->transforms[tile].rotation *= quat(0.0f, r180, 0.0f);
                    
                    if(cv.y < 0.0f)
                        scene->transforms[tile].rotation *= quat(-r90, 0.0f, 0.0f);
                    
                    scene->entities[tile] |= e_cmp::transform;
                    sb_push(block_entities, tile);

                    continue;
                }
                else if (!cn[c][2])
                {
                    u32 tile = get_new_entity(scene);
                    scene->geometry_names[tile] = file_top_side;
                    scene->transforms[tile].translation = cpc;
                    scene->transforms[tile].rotation = quat(0.0f, -r90, 0.0f);
                    scene->transforms[tile].scale = vec3f::one();
                    
                    if(cv.z < 0.0f)
                        scene->transforms[tile].rotation *= quat(0.0f, r180, 0.0f);
                    
                    if(cv.y < 0.0f)
                        scene->transforms[tile].rotation *= quat( -r90, 0.0f, 0.0f);
                    
                    scene->entities[tile] |= e_cmp::transform;
                    sb_push(block_entities, tile);

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

            u32 tile = get_new_entity(scene);
            scene->geometry_names[tile] = model;
            scene->transforms[tile].translation = cpc;
            scene->transforms[tile].rotation = corner_face_rotations[c][rot_i];
            scene->transforms[tile].scale = vec3f::one();
            scene->entities[tile] |= e_cmp::transform;
            sb_push(block_entities, tile);
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
                    u32 tile = get_new_entity(scene);
                    scene->geometry_names[tile] = model;
                    scene->transforms[tile].translation = esubp[i];
                    scene->transforms[tile].rotation = rot_r;
                    scene->transforms[tile].scale = vec3f::one();
                    scene->entities[tile] |= e_cmp::transform;
                    sb_push(block_entities, tile);
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
            vec3f fp = pos + axis[f] * 0.5f;
            vec3f fv = axis[f];
            vec3f fpc = fp + fv * -0.125f;
            
            vec3f u = axis_t[f];
            vec3f v = axis_bt[f];
            
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
                u32 tile = get_new_entity(scene);
                scene->geometry_names[tile] = file_top_centre;
                scene->transforms[tile].translation = fpsub[i];
                scene->transforms[tile].rotation = face_rotation[f];
                scene->transforms[tile].scale = vec3f::one();
                scene->entities[tile] |= e_cmp::transform;
                sb_push(block_entities, tile);
            }

            put::dbg::add_point(fp, 0.5f);
        }
    }
    
    // bake vertex buffer
    u32 num_tiles = sb_count(block_entities);
    
    for(u32 i = 0; i < num_tiles; ++i)
    {
        u32 tb = block_entities[i];
        
        scene->parents[tb] = parent;
        scene->bounding_volumes[tb].min_extents = vec3f(-0.125f);
        scene->bounding_volumes[tb].max_extents = vec3f( 0.125f);
    }

    // need to bake world mats and extents
    update_scene(scene, 0.0f);
    
    // bake vertex buffer
    bake_entities_to_vb(scene, parent, block_entities);
    
    sb_free(block_entities);
}

void setup_level_editor(put::ecs::ecs_scene* scene)
{
    load_pmm("data/models/environments/general/basic_top_corner.pmm", scene, e_pmm_load_flags::geometry | e_pmm_load_flags::material);
    load_pmm("data/models/environments/general/basic_middle_corner.pmm", scene, e_pmm_load_flags::geometry  | e_pmm_load_flags::material);
    load_pmm("data/models/environments/general/basic_top_side.pmm", scene, e_pmm_load_flags::geometry  | e_pmm_load_flags::material);
    load_pmm("data/models/environments/general/basic_middle_side.pmm", scene, e_pmm_load_flags::geometry  | e_pmm_load_flags::material);
    load_pmm("data/models/environments/general/basic_top_center.pmm", scene, e_pmm_load_flags::geometry  | e_pmm_load_flags::material);
    
    physics::physics_consume_command_buffer();
    pen::thread_sleep_ms(4);
}

void setup_level(put::ecs::ecs_scene* scene)
{
    material_resource* default_material = get_material_resource(PEN_HASH("default_material"));
    geometry_resource* box_resource = get_geometry_resource(PEN_HASH("cube"));
        
    u32 b = get_new_entity(scene);
    scene->names[b] = "ground";
    scene->transforms[b].translation = vec3f(0.0f, -1.0f, 0.0f);;
    scene->transforms[b].rotation = quat();
    scene->transforms[b].scale = vec3f(100.0f, 1.0f, 100.0f);
    scene->entities[b] |= e_cmp::transform;
    scene->parents[b] = b;
    scene->physics_data[b].rigid_body.shape = physics::e_shape::box;
    scene->physics_data[b].rigid_body.mass = 0.0f;
    scene->physics_data[b].rigid_body.group = 1;
    scene->physics_data[b].rigid_body.mask = 0xffffffff;

    instantiate_geometry(box_resource, scene, b);
    instantiate_material(default_material, scene, b);
    instantiate_model_cbuffer(scene, b);
    instantiate_rigid_body(scene, b);
}

void instantiate_mushroom(put::ecs::ecs_scene* scene, dr_ecs_exts* ext, vec3f pos)
{
    u32 root = load_pmm("data/models/props/mushroom01.pmm", scene);
    
    vec3f dim = scene->bounding_volumes[root].max_extents - scene->bounding_volumes[root].min_extents;
    
    // add capsule for collisions
    scene->physics_data[root].rigid_body.shape = physics::e_shape::capsule;
    scene->physics_data[root].rigid_body.mass = 0.0f;
    
    //scene->physics_data[root].rigid_body.group = 4;
    //scene->physics_data[root].rigid_body.mask = ~1;
    
    scene->physics_data[root].rigid_body.group = 2;
    scene->physics_data[root].rigid_body.mask = 0xffffffff;
    
    scene->physics_data[root].rigid_body.dimensions = dim * 0.5f;
    scene->physics_data[root].rigid_body.create_flags |= (physics::e_create_flags::dimensions);

    // drs feet are at 0.. offset collision to centre at 0.5
    scene->physics_offset[root].translation = vec3f(0.0f, dim.y * 0.5f, 0.0f);
    
    scene->transforms[root].translation = pos;
    
    instantiate_rigid_body(scene, root);
    
    ext->cmp_flags[root] |= e_game_cmp::collectable;
}

void instantiate_blob(put::ecs::ecs_scene* scene, dr_ecs_exts* ext, vec3f pos)
{
    u32 root = load_pmm("data/models/characters/blob/blob.pmm", scene);
    
    anim_handle idle = load_pma("data/models/characters/blob/anims/blob_idle.pma");
    
    bind_animation_to_rig(scene, idle, root);
    
    // add capsule for collisions
    scene->physics_data[root].rigid_body.shape = physics::e_shape::cone;
    scene->physics_data[root].rigid_body.mass = 1.0f;
    scene->physics_data[root].rigid_body.group = 4;
    scene->physics_data[root].rigid_body.mask = ~1;
    scene->physics_data[root].rigid_body.dimensions = vec3f(0.6f, 0.5f, 0.6f);
    scene->physics_data[root].rigid_body.create_flags |= (physics::e_create_flags::dimensions | physics::e_create_flags::kinematic);

    // drs feet are at 0.. offset collision to centre at 0.5
    scene->physics_offset[root].translation = vec3f(0.0f, 0.5f, 0.0f);
    
    scene->transforms[root].translation = pos;
    
    instantiate_rigid_body(scene, root);
    
    ext->cmp_flags[root] |= e_game_cmp::blob;
}

void setup_character(put::ecs::ecs_scene* scene)
{
    // load main model
    dr.root = load_pmm("data/models/characters/doctor/doctor.pmm", scene);
    
    // load anims
    anim_handle idle = load_pma("data/models/characters/doctor/anims/doctor_idle01.pma");
    anim_handle walk = load_pma("data/models/characters/doctor/anims/doctor_walk.pma");
    anim_handle run = load_pma("data/models/characters/doctor/anims/doctor_run.pma");
    anim_handle jump = load_pma("data/models/characters/doctor/anims/doctor_idle_jump.pma");
    anim_handle run_l = load_pma("data/models/characters/doctor/anims/doctor_run_l.pma");
    anim_handle run_r = load_pma("data/models/characters/doctor/anims/doctor_run_r.pma");
    anim_handle attack = load_pma("data/models/characters/doctor/anims/doctor_attack01.pma");
    
    // bind to rig
    dr.anim_idle = bind_animation_to_rig(scene, idle, dr.root);
    dr.anim_walk = bind_animation_to_rig(scene, walk, dr.root);
    dr.anim_run = bind_animation_to_rig(scene, run, dr.root);
    dr.anim_jump = bind_animation_to_rig(scene, jump, dr.root);
    dr.anim_run_l = bind_animation_to_rig(scene, run_l, dr.root);
    dr.anim_run_r  = bind_animation_to_rig(scene, run_r, dr.root);
    dr.anim_attack = bind_animation_to_rig(scene, attack, dr.root);

    // add capsule for collisions
    scene->physics_data[dr.root].rigid_body.shape = physics::e_shape::capsule;
    scene->physics_data[dr.root].rigid_body.mass = 1.0f;
    scene->physics_data[dr.root].rigid_body.group = 4;
    scene->physics_data[dr.root].rigid_body.mask = ~1;
    scene->physics_data[dr.root].rigid_body.dimensions = vec3f(0.3f, 0.3f, 0.3f);
    scene->physics_data[dr.root].rigid_body.create_flags |= (physics::e_create_flags::dimensions | physics::e_create_flags::kinematic);

    // drs feet are at 0.. offset collision to centre at 0.5
    scene->physics_offset[dr.root].translation = vec3f(0.0f, 0.5f, 0.0f);

    instantiate_rigid_body(scene, dr.root);

    physics::set_v3(scene->physics_handles[dr.root], vec3f::zero(), physics::e_cmd::set_angular_velocity);
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

u32 find_entity_from_physics(ecs_scene* scene, u32 physics_handle)
{
    for(u32 n = 0; n < scene->num_entities; ++n)
    {
        if(scene->physics_handles[n] == physics_handle)
            return n;
    }
    
    return PEN_INVALID_HANDLE;
}

void find_islands(ecs_scene* scene, dr_ecs_exts* ext, u32 entity, u32** island_list)
{
    // detect neighbours
    u32 neighbours[6];
    detect_neighbours_ex(scene->transforms[entity].translation, 1.0f, neighbours, scene, ext);
    
    // inner blocks can be discarded
    u32 mask = 0;
    for(u32 nn = 0; nn < 6; ++nn)
    {
        if(is_valid(neighbours[nn]))
            mask |= 1<<nn;
    }
    
    if(!mask)
    {
        ext->cmp_flags[entity] = 0;
        ecs::delete_entity(scene, entity);
        return;
    }
    
    // add island
    ext->tile_blocks[entity].neighbour_mask = mask;
    ext->game_flags[entity] |= e_game_flags::tile_in_island;
    sb_push(*island_list, entity);
    
    // entity index from physics
    u32 neighbour_entity[6];
    for(u32 nn = 0; nn < 6; ++nn)
        neighbour_entity[nn] = find_entity_from_physics(scene, neighbours[nn]);
    
    for(u32 en = 0; en < 6; ++en)
    {
        u32 entity = neighbour_entity[en];
        if(entity == PEN_INVALID_HANDLE)
            continue;
        
        if(!(ext->cmp_flags[entity] & e_game_cmp::tile_block))
            continue;
        
        if((ext->game_flags[entity] & e_game_flags::tile_in_island))
            continue;
        
        // recurse, adding its neighbours
        find_islands(scene, ext, entity, island_list);
    }
}

bool check_occupied(vec3f bp, u32& ph)
{
    physics::ray_cast_params rcp;
    rcp.start = bp + vec3f(0.0f, 1.0f, 0.0f);
    rcp.end = bp;
    rcp.group = 1;
    rcp.mask = 1;
    
    cast_result cast = physics::cast_ray_immediate(rcp);
        
    ph = 0;
    if(cast.set)
    {
        ph = cast.physics_handle;
        return true;
    }
    
    return false;
}

bool detect_inner_block(vec3f block, vec3f* list)
{
    u32 num_list = sb_count(list);
    for(u32 i = 0; i < 6; ++i)
    {
        bool found = false;
        for(u32 l = 0; l < num_list; ++l)
        {
            vec3f nn = list[l];
            if(maths::point_inside_sphere(block + axis[i], 1.0f, nn))
            {
                found = true;
                break;
            }
        }
        
        if(!found)
            return false;
    }
    
    return true;
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
    
    ImGui::SameLine();
    
    static u32** islands = nullptr;
    if (ImGui::Button("Build Islands"))
    {
        //clear islands
        sb_clear(islands);
        
        //clear flags
        for(u32 n = 0; n < scene->num_entities; ++n)
            ext->game_flags[n] &= ~e_game_flags::tile_in_island;
        
        for(u32 n = 0; n < scene->num_entities; ++n)
        {
            if(!(ext->cmp_flags[n] & e_game_cmp::tile_block))
                continue;
            
            if((ext->game_flags[n] & e_game_flags::tile_in_island))
                continue;
            
            if(ext->tile_blocks[n].flags & e_tile_flags::inner)
                continue;
            
            u32* island = nullptr;
            find_islands(scene, ext, n, &island);
            
            // make tile blocks children
            u32 island_id = get_new_entity(scene);
            u32 island_parent = island_id;
            scene->names[island_id].setf("island_%i", island_id);
            scene->transforms[island_id].translation = vec3f::zero();
            scene->transforms[island_id].scale = vec3f::one();
            scene->transforms[island_id].rotation = quat();
            scene->entities[island_id] |= e_cmp::transform;
            
            u32 num_blocks = sb_count(island);
            if(num_blocks > 0)
            {
                for(u32 i = 0; i < num_blocks; ++i)
                {
                    u32 tb = island[i];
                    set_entity_parent_validate(scene, island_parent, tb);
                    ext->tile_blocks[tb].island_id = island_id;
                    scene->names[tb].setf("island_%i_block_%i", island_id, i);
                    island[i] = tb; // tb might have changed
                }
            }
            
            scene->flags |= e_scene_flags::invalidate_scene_tree;
            
            sb_push(islands, island);
        }

        u32 num_islands = sb_count(islands);
        for(u32 i = 0; i < num_islands; ++i)
        {
            vec3f* verts = nullptr;
            
            u32 num_tiles = sb_count(islands[i]);
            if(num_tiles == 0)
                continue;
            
            u32 island_parent = scene->parents[islands[i][0]];
            
            scene->physics_data[island_parent].rigid_body.shape = physics::e_shape::mesh;
            scene->physics_data[island_parent].rigid_body.mass = 0.0f;
            
            bake_tile_blocks(scene, ext, islands[i], num_tiles, island_parent);
            
            for(u32 t = 0; t < num_tiles; ++t)
            {
                u32 tb = islands[i][t];
                vec3f tp = scene->transforms[tb].translation;
                
                for(u32 n = 0; n < 6; ++n)
                {
                    if(ext->tile_blocks[tb].neighbour_mask & (1<<n))
                        continue;
                    
                    // normal
                    vec3f mm = tp + axis[n] * 0.5f;
                    
                    // 4 verts
                    vec3f v[] =
                    {
                        mm - axis_t[n] * 0.5f - axis_bt[n] * 0.5f, // --
                        mm + axis_t[n] * 0.5f - axis_bt[n] * 0.5f, // +-
                        mm + axis_t[n] * 0.5f + axis_bt[n] * 0.5f, // ++
                        mm - axis_t[n] * 0.5f + axis_bt[n] * 0.5f  // -+
                    };
                    
                    if(!(n == 2 || n == 5))
                        std::swap(v[0], v[2]);

                    sb_push(verts, v[0]);
                    sb_push(verts, v[1]);
                    sb_push(verts, v[2]);
                    
                    sb_push(verts, v[2]);
                    sb_push(verts, v[3]);
                    sb_push(verts, v[0]);
                }

                ecs::delete_entity(scene, tb);
            }
            
            collision_mesh_from_vertex_list((f32*)&verts[0],
                                            sb_count(verts), scene->physics_data[island_parent].rigid_body.mesh_data);
            
            instantiate_rigid_body(scene, island_parent);
        }
        
        // delete all inners
        u32 ne = scene->num_entities;
        for(u32 e = 0; e < ne; ++e)
        {
            if(ext->cmp_flags[e] & e_game_cmp::tile_block)
                if(ext->tile_blocks[e].flags & e_tile_flags::inner)
                    ecs::delete_entity(scene, e);
        }
        
        // trim num entities to reduce usage
        ecs::trim_entities(scene);
    }
    
    static s32 edit_axis = 1;
    static const c8* edit_axis_name[] =
    {
        "zy",
        "xz",
        "xy"
    };
    
    ImGui::Combo("Edit Axis", &edit_axis, &edit_axis_name[0], 3);
    ImGui::InputInt("Level", &slice[edit_axis]);
    
    pN = axis[edit_axis];
    p0[edit_axis] = slice[edit_axis];
    
    static bool _dbu = false;
    if(press_debounce(PK_1, _dbu))
        slice[edit_axis]++;
    
    static bool _dbd = false;
    if(press_debounce(PK_2, _dbd))
        slice[edit_axis]--;

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
    
    vec3f altpN = axis[(edit_axis+1)%3];
    vec3f altip = maths::ray_plane_intersect(r0, rv, p0, altpN);
    
    // snap to grid
    ip = floor(ip) + vec3f(0.5f);
    ip[edit_axis] = slice[edit_axis]-0.5f;
    
    // tile
    if(!ms.buttons[PEN_MOUSE_L])
        dbg::add_aabb(ip - vec3f(0.5f), ip + vec3f(0.5f), vec4f::white());
    
    // focus
    static bool _dbf;
    if(press_debounce(PK_F, _dbf))
        ecsc.camera->focus = ip;
    
    // drag marquee
    static vec3f down_ip = ip;
    static vec3f down_altip = altip;
    static bool _dbm = false;
    static vec3f* selected = nullptr;
    static vec3f* island_selected = nullptr;
    
    if(ms.buttons[PEN_MOUSE_L])
    {
        if(!_dbm)
        {
            down_ip = ip;
            down_altip = altip;
        }
        
        u32 ph = 0;
        if(check_occupied(down_ip, ph))
        {
            if(!_dbm)
            {
                // detect island
                sb_clear(island_selected);
                
                u32 e = find_entity_from_physics(scene, ph);
                
                u32* island = nullptr;
                find_islands(scene, ext, e, &island);
                
                u32 num_tb = sb_count(island);
                for(u32 i = 0; i < num_tb; ++i)
                {
                    vec3f sp = scene->transforms[island[i]].translation;
                    sb_push(island_selected, sp);
                }
            }
            else
            {
                // extrude island in the planes axis
                u32 num_island_selected = sb_count(island_selected);

                vec3f vdrag = altip - down_altip;
                
                u32 num = abs(dot(vdrag, pN));
                f32 sign = dot(vdrag, pN) < 0.0f ? -1.0f : 1.0f;
                
                vec3f off = vec3f::zero();
                
                sb_clear(selected);
                for(u32 i = 0; i <= num; ++i)
                {
                    for(u32 s = 0; s < num_island_selected; ++s)
                    {
                        vec3f op = island_selected[s] + off;
                        put::dbg::add_aabb(op - vec3f(0.5f), op + vec3f(0.5f));
                        sb_push(selected, op);
                    }
                    
                    off += pN * sign;
                }
            }
        }
        else
        {
            // drag axis
            vec3f vdrag = ip - down_ip;
            
            u32 num_axis[3] = {
                0, 0, 0
            };
            
            f32 sign_axis[3] = {
                1.0f, 1.0f, 1.0f
            };
            
            for(u32 vv = 0; vv < 3; ++vv)
            {
                num_axis[vv] = abs(vdrag[vv]);
                sign_axis[vv] = vdrag[vv] < 0.0f? -1.0f : 1.0f;
            }
            
            vec3f dmm = down_ip - vec3f(0.5f);
            vec3f dmx = down_ip + vec3f(0.5f);
            
            vec3f off = vec3f::zero();
            
            sb_clear(selected);
            for(u32 x = 0; x <= num_axis[0]; ++x)
            {
                off.x = x * sign_axis[0];
                
                for(u32 y = 0; y <= num_axis[1]; ++y)
                {
                    off.y = y * sign_axis[1];
                    
                    for(u32 z = 0; z <= num_axis[2]; ++z)
                    {
                        off.z = z * sign_axis[2];
                        
                        sb_push(selected, dmm + off + vec3f(0.5f));
                        
                        put::dbg::add_aabb(dmm + off, dmx + off);
                    }
                }
            }
        }

        _dbm = true;
    }
    else
    {
        u32 num_selected = sb_count(selected);
        for(u32 s = 0; s < num_selected; ++s)
        {
            // detect current occupancy
            vec3f bp = selected[s];
            
            physics::ray_cast_params rcp;
            rcp.start = bp + vec3f(0.0f, 1.0f, 0.0f);
            rcp.end = bp;
            rcp.group = 1;
            rcp.mask = 1;
            
            cast_result cast = physics::cast_ray_immediate(rcp);
            
            if(cast.set)
                continue;
            
            if(!detect_inner_block(bp, selected))
            {
                add_tile_block(scene, ext, selected[s]);
            }
            else
            {
                u32 nn = get_new_entity(scene);
                ext->cmp_flags[nn] |= e_game_cmp::tile_block;
                ext->tile_blocks[nn].flags |= e_tile_flags::inner;
                scene->transforms[nn].translation = selected[s];
            }
        }
        sb_clear(selected);
        
        _dbm = false;
    }
    
    // draw islands
    vec4f cols_test[] = {
        vec4f::yellow(),
        vec4f::green(),
        vec4f::blue(),
        vec4f::red()
    };
    
    u32 num_islands = sb_count(islands);
    for(u32 i = 0; i < num_islands; ++i)
    {
        u32 num_tiles = sb_count(islands[i]);
        for(u32 t = 0; t < num_tiles; ++t)
        {
            u32 entity = islands[i][t];
            vec3f p = scene->transforms[entity].translation;
            dbg::add_aabb(p - vec3f(0.5f), p + vec3f(0.5f), cols_test[i%4]);
        }
    }
    
    for(u32 n = 0; n < 6; ++n)
    {
        vec3f nip = ip + axis[n] * 100000.0f;
        
        physics::ray_cast_params rcp;
        rcp.start = ip;
        rcp.end = nip;
        rcp.group = 1;
        rcp.mask = 1;
        
        cast_result cast = physics::cast_ray_immediate(rcp);
        
        // ..
        if(cast.set)
        {
            dbg::add_point(cast.point, 0.1f, vec4f::yellow());
            
            for(u32 i = 0; i < 4; ++i)
            {
                dbg::add_line(cast.point, cast.point + cast.normal);
            }
        }
    }
}

void get_controller_input(camera* cam, ecs_scene* scene, controller_input& ci)
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
            ci.actions |= e_contoller::run;
        }
        
        if(gs.button[PGP_BUTTON_A])
        {
            ci.actions |= e_contoller::jump;
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
             ci.actions |= e_contoller::jump;
        }
        
        if (pen::input_key(PK_E))
        {
             ci.actions |= e_contoller::attack;
        }
        
        if (pen::input_key(PK_SHIFT))
        {
            ci.actions |= e_contoller::run;
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
    
    // mouse movement
    if(pen::input_mouse(PEN_MOUSE_L))
    {
        pen::mouse_state ms = pen::input_get_mouse_state();
        
        vec2i vpi = vec2i(pen_window.width, pen_window.height);
        mat4  view_proj = cam->proj * cam->view;
        vec3f r0 = maths::unproject_sc(vec3f(ms.x, pen_window.height - ms.y, 0.0f), view_proj, vpi);
        vec3f r1 = maths::unproject_sc(vec3f(ms.x, pen_window.height - ms.y, 1.0f), view_proj, vpi);
        
        physics::ray_cast_params rcp;
        rcp.start = r0;
        rcp.end = r1;
        rcp.mask = 0xffffff;
        rcp.group = 0xffffff;
        
        physics::cast_result surface_cast = physics::cast_ray_immediate(rcp);
        
        if(surface_cast.set)
        {
            vec3f wp = surface_cast.point * vec3f(1.0f, 0.0f, 1.0f);
            vec3f dp = scene->transforms[dr.root].translation * vec3f(1.0f, 0.0f, 1.0f);
            
            ci.movement_dir = normalised(wp - dp);
            ci.movement_vel = 1.0f;
        }
    }
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
    static bool game_cam = true;

    static u32 hp = 0;
    static f32 pos_hist_x[60];
    static f32 pos_hist_y[60];
    static f32 pos_hist_z[60];

    // controller ----------------------------------------------------------------------------------------------------------
    
    get_controller_input(camera, scene, ci);
    
    dt = min(dt, 1.0f / 10.0f);

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

    static u32 _loco[] =
    {
        dr.anim_run,
        dr.anim_walk
    };
    
    if(pc.loco_vel == 0.0f)
    {
        // idle state
        controller.blend.anim_a = dr.anim_idle;
        controller.blend.anim_b = dr.anim_walk;
        
        for(u32 aa = 0; aa < 2; ++aa)
        {
            controller.anim_instances[_loco[aa]].time = 0.0f;
            controller.anim_instances[_loco[aa]].root_delta = vec3f::zero();
            controller.anim_instances[_loco[aa]].flags |= (e_anim_flags::paused | e_anim_flags::looped);
        }
    }
    else
    {
        controller.anim_instances[dr.anim_run].flags &= ~e_anim_flags::paused;
        controller.anim_instances[dr.anim_walk].flags &= ~e_anim_flags::paused;
        
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
    if (pc.air == 0 && !(ci.actions & e_contoller::jump))
        pc.actions &= ~e_contoller::debounce_jump;

    // must debounce
    if (pc.actions & e_contoller::debounce_jump)
        ci.actions &= ~e_contoller::jump;
    
    if (ci.actions & e_contoller::jump && pc.air <= jump_time)
    {
        pc.acc.y += jump_strength;
        
        if(pc.air == 0.0f)
        {
            pc.air_vel = smooth_step(pc.loco_vel, 0.0f, 5.0f, min_jump_vel, max_jump_vel);
        }
        
        pc.air = max(pc.air, dt);
    }
    else if(ci.actions & e_contoller::jump)
    {
        // must release to re-jump
        pc.actions |= e_contoller::debounce_jump;
    }

    // apply jump anim
    if (pc.air > 0.0f)
    {
        pc.acc += ci.movement_dir * ci.movement_vel * pc.air_vel;

        if (pc.air > 1.0f/10.0f)
        {
            controller.blend.anim_a = dr.anim_jump;
            controller.blend.anim_b = dr.anim_jump;
        }
    }
    
    // apply attack anim
    if(ci.actions & e_contoller::attack)
    {
        controller.blend.anim_a = dr.anim_attack;
        controller.blend.anim_b = dr.anim_attack;
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
    
    if (!(scene->flags & e_scene_flags::pause_update))
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
    physics::sphere_cast_params scp;
    scp.from = mid - ci.movement_dir * 0.3f;
    scp.to = mid + ci.movement_dir * 1000.0f;
    scp.dimension = vec3f(0.3f);
    scp.group = 1;
    scp.mask = 1;

    physics::cast_result wall_cast = physics::cast_sphere_immediate(scp);
    
    physics::ray_cast_params rcp;
    rcp.start = feet;
    rcp.end = feet + vec3f(0.0f, -10000.0f, 0.0f);
    rcp.group = 1;
    rcp.mask = 0xff;

    physics::cast_result surface_cast = physics::cast_ray_immediate(rcp);
    
    // walls
    vec3f cv = mid - wall_cast.point;
    if (mag(cv) < 0.3f && wall_cast.set)
    {
        f32 diff = 0.3f - mag(cv);
        pc.pos += wall_cast.normal * diff;
    }
    
    // floor collision from casts
    f32 cvm2 = mag(mid - surface_cast.point);
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
    if (!(scene->flags & e_scene_flags::pause_update))
    {
        scene->initial_transform[5].rotation = quat(0.0f, ci.dir_angle, 0.0f);
        scene->transforms[dr.root].translation = pc.pos;
        scene->entities[dr.root] |= e_cmp::transform;
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
        put::dbg::add_point(surface_cast.point, 0.1f, vec4f::green());
        put::dbg::add_line(surface_cast.point, surface_cast.point + pc.surface_normal, vec4f::cyan());

        vec3f smid = surface_cast.point + vec3f(0.0f, 0.5f, 0.0f);
        put::dbg::add_line(smid, smid + pc.surface_perp, vec4f::red());

        put::dbg::add_circle(vec3f::unit_y(), mid, 0.3f, vec4f::green());
        put::dbg::add_point(wall_cast.point, 0.1f, vec4f::green());

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
    
    vec3f dr_pos = scene->world_matrices[dr.root].get_translation();
    
    for(u32 n = 0; n < scene->num_entities; ++n)
    {
        if(ext->cmp_flags[n] & e_game_cmp::custom_anim)
        {
            scene->transforms[n].rotation *= quat(0.0f, dt, 0.0f);
            scene->entities[n] |= e_cmp::transform;
        }
        
        // blob ai
        if(ext->cmp_flags[n] & e_game_cmp::blob)
        {
            // goto player
            
            vec3f p = scene->transforms[n].translation;
            
            vec3f vv = normalised((dr_pos - p) * vec3f(1.0f, 0.0f, 1.0f));
            
            scene->transforms[n].translation += vv * 0.01f;
            
            f32 r = atan2(vv.x, vv.z);
            
            scene->transforms[n].rotation = quat(0.0f, r, 0.0f);
            
            // avoid stuff
            // ..
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
    
    put::scene_view_renderer svr_shadow_maps;
    svr_shadow_maps.name = "ces_render_shadow_maps";
    svr_shadow_maps.id_name = PEN_HASH(svr_shadow_maps.name.c_str());
    svr_shadow_maps.render_function = &render_shadow_views;

    put::scene_view_renderer svr_area_light_textures;
    svr_area_light_textures.name = "ces_render_area_light_textures";
    svr_area_light_textures.id_name = PEN_HASH(svr_area_light_textures.name.c_str());
    svr_area_light_textures.render_function = &ecs::render_area_light_textures;
    
    put::scene_view_renderer svr_omni_shadow_maps;
    svr_omni_shadow_maps.name = "ces_render_omni_shadow_maps";
    svr_omni_shadow_maps.id_name = PEN_HASH(svr_omni_shadow_maps.name.c_str());
    svr_omni_shadow_maps.render_function = &ecs::render_omni_shadow_views;
    
    pmfx::register_scene_view_renderer(svr_main);
    pmfx::register_scene_view_renderer(svr_editor);
    pmfx::register_scene_view_renderer(svr_shadow_maps);
    pmfx::register_scene_view_renderer(svr_area_light_textures);
    pmfx::register_scene_view_renderer(svr_omni_shadow_maps);
    
    pmfx::register_scene(main_scene, "main_scene");
    pmfx::register_camera(&main_camera, "model_viewer_camera");

    pmfx::init("data/configs/editor_renderer.jsn");
    put::init_hot_loader();
    
    setup_character(main_scene);
    setup_level_editor(main_scene);
    setup_level(main_scene);
    
    for(u32 i = 0; i < 5; ++i)
    {
        vec3f pos = vec3f(rand()%40 - 20, 0.0f, rand()%40 - 20);
        instantiate_blob(main_scene, exts, pos);
    }
    
    for(u32 i = 0; i < 20; ++i)
    {
        vec3f pos = vec3f(rand()%40 - 20, 0.0f, rand()%40 - 20);
        instantiate_mushroom(main_scene, exts, pos);
    }
    
    // lights
    vec3f lp[] = {
        vec3f(10.0f, 5.0f, 3.0f),
        vec3f(-2.0f, 5.0f, 12.0f),
        vec3f(-13.0f, 5.0f, -8.0f),
        vec3f(5.0f, 5.0f, -5.0f),
    };
    
    vec4f lc[] = {
        vec4f::orange(),
        vec4f::magenta(),
        vec4f::green(),
        vec4f::cyan()
    };
    
    main_scene->lights[0].colour = vec3f(0.1f, 0.1f, 0.1f);
    
    for(u32 i = 0; i < 4; ++i)
    {
        u32 light = get_new_entity(main_scene);
        instantiate_light(main_scene, light);
        main_scene->names[light] = "point_light";
        main_scene->id_name[light] = PEN_HASH("point_light");
        main_scene->lights[light].colour = lc[i].xyz;
        main_scene->lights[light].radius = 10.0f;
        main_scene->lights[light].type = e_light_type::point;
        main_scene->lights[light].shadow_map = true;
        main_scene->transforms[light].translation = lp[i];
        main_scene->transforms[light].rotation = quat();
        main_scene->transforms[light].scale = vec3f::one();
        main_scene->entities[light] |= e_cmp::light;
        main_scene->entities[light] |= e_cmp::transform;
    }
    
    main_scene->view_flags |= e_scene_view_flags::hide_debug;
    put::dev_ui::enable(false);
    
    while( 1 )
    {
        static timer* frame_timer = pen::timer_create();
        pen::timer_start(frame_timer);

		put::dev_ui::new_frame();
        
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
