dofile "../pmtech/tools/premake/options.lua"
dofile "../pmtech/tools/premake/globals.lua"
dofile "../pmtech/tools/premake/app_template.lua"

-- Solution
solution ("dr_scientist_" .. platform_dir)
	location ("build/" .. platform_dir ) 
	configurations { "Debug", "Release" }
	startproject "dr_scientist"
	buildoptions { build_cmd }
	linkoptions { link_cmd }
	
-- Engine Project	
dofile "../pmtech/core/pen/project.lua"
dofile "../pmtech/core/put/project.lua"

-- Example projects	
-- ( project name, current script dir, )
create_app( "dr_scientist", "", script_path() )

	
	
