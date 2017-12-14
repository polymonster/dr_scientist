--add extension options
newoption 
{
   trigger     = "renderer",
   value       = "API",
   description = "Choose a renderer",
   allowed = 
   {
      { "opengl", "OpenGL" },
      { "dx11",  "DirectX 11 (Windows only)" },
   }
}

newoption 
{
   trigger     = "sdk_version",
   value       = "version",
   description = "Specify operating system SDK",
}

newoption 
{
   trigger     = "platform_dir",
   value       = "dir",
   description = "specify platform specifc src folder",
}

newoption 
{
   trigger     = "xcode_target",
   value       = "TARGET",
   description = "Choose an xcode build target",
   allowed = 
   {
      { "osx", "OSX" },
      { "ios",  "iOS" },
   }
}

dofile "../pmtech/tools/premake/globals.lua"
dofile "../pmtech/tools/premake/app_template.lua"

-- Solution
solution "dr_scientist"
	location ("build/" .. platform_dir ) 
	configurations { "Debug", "Release" }
	startproject "dr_scientist"
	buildoptions { build_cmd }
	linkoptions { link_cmd }
	
-- Engine Project	
dofile "../pmtech/pen/project.lua"

-- Toolkit Project	
dofile "../pmtech/put/project.lua"

-- Example projects	
-- ( project name, current script dir, )
create_app( "dr_scientist", script_path() )

	
	
