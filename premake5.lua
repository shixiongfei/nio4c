solution ( "nio4c" )
  configurations { "Release", "Debug" }
  platforms { "x64" }

  if _ACTION == "clean" then
    os.rmdir(".vs")
    os.rmdir("test")
    os.remove("nio4c.VC.db")
    os.remove("nio4c.sln")
    os.remove("nio4c.vcxproj")
    os.remove("nio4c.vcxproj.filters")
    os.remove("nio4c.vcxproj.user")
    os.remove("nio4c.make")
    os.remove("Makefile")
    return
  end

  -- A project defines one build target
  project ( "nio4c" )
  kind ( "ConsoleApp" )
  language ( "C" )
  targetname ("nio4c")
  files { "./*.h", "./*.c" }
  defines { "_UNICODE" }
  staticruntime "On"

  configuration ( "Release" )
    optimize "On"
    objdir ( "./test/tmp" )
    targetdir ( "./test" )
    defines { "NDEBUG", "_NDEBUG" }

  configuration ( "Debug" )
    symbols "On"
    objdir ( "./test/tmp" )
    targetdir ( "./test" )
    defines { "DEBUG", "_DEBUG" }

  configuration ( "vs*" )
    defines { "WIN32", "_WIN32", "_WINDOWS",
              "_CRT_SECURE_NO_WARNINGS", "_CRT_SECURE_NO_DEPRECATE",
              "_CRT_NONSTDC_NO_DEPRECATE", "_WINSOCK_DEPRECATED_NO_WARNINGS" }
    links { "Ws2_32", "IPHLPAPI" }

  configuration ( "gmake" )
    warnings  "Default" --"Extra"
    defines { "LINUX_OR_MACOSX" }
    links { "iconv" }

  configuration { "gmake", "macosx" }
    defines { "__APPLE__", "__MACH__", "__MRC__", "macintosh" }
    links { "Foundation.framework", "IOKit.framework" }

  configuration { "gmake", "linux" }
    defines { "__linux__" }
    links { "m" }
