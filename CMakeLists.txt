cmake_minimum_required(VERSION 3.22)
project(DjehutiCAD VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FRUSTLANG_ROOT "D:/FrustLang" CACHE PATH "Path to the FrustLang research repository")
set(FRUST_VCPKG_INSTALLED_DIR "D:/000 Creation Suite/apps/CreationEngine/vcpkg_installed/x64-windows" CACHE PATH "Existing LLVM/vcpkg installation used by Frust")
set(FLEX_EXECUTABLE "D:/tools/winflexbison/win_flex.exe" CACHE FILEPATH "Flex for the Frust grammar")
set(BISON_EXECUTABLE "D:/tools/winflexbison/win_bison.exe" CACHE FILEPATH "Bison for the Frust grammar")
set(FRUST_WINFLEXBISON_DIR "D:/tools/winflexbison" CACHE PATH "WinFlexBison directory")
set(OCCT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/deps/occt-build" CACHE PATH "Pinned OCCT build used by DjehutiCAD")
set(OCCT_INCLUDE_DIR "${OCCT_ROOT}/inc")
set(OCCT_LIBRARY_DIR "${OCCT_ROOT}/win64/vc14/lib")

if(NOT TARGET juce::juce_core)
    if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
        add_subdirectory("${JUCE_PATH}" juce_build)
    elseif(DEFINED ENV{JUCE_PATH} AND EXISTS "$ENV{JUCE_PATH}/CMakeLists.txt")
        add_subdirectory("$ENV{JUCE_PATH}" juce_build)
    elseif(EXISTS "D:/JUCE/CMakeLists.txt")
        add_subdirectory("D:/JUCE" juce_build)
    else()
        message(FATAL_ERROR "JUCE not found. Set JUCE_PATH to a JUCE checkout.")
    endif()
endif()

if(NOT TARGET CreationDock)
    add_subdirectory("${FRUSTLANG_ROOT}/projects/03_creation_dock" creation_dock_build)
endif()
if(NOT TARGET ai_provider)
    add_subdirectory("${FRUSTLANG_ROOT}/projects/04_ai_provider" ai_provider_build)
endif()
if(NOT TARGET frate_lib)
    add_subdirectory("${FRUSTLANG_ROOT}/projects/05_frate" frust_frate_build)
endif()
if(NOT TARGET frust_lang)
    add_subdirectory("${FRUSTLANG_ROOT}/projects/01_language_paradigms/02_functional" frust_lang_build)
endif()
if(NOT TARGET frust_plugin_host)
    add_subdirectory("${FRUSTLANG_ROOT}/projects/09_frust_plugin_host" frust_plugin_host_build)
endif()

if(MSVC)
    foreach(frust_target frate_lib frust_lang frust_runtime frust_plugin_host)
        if(TARGET ${frust_target})
            target_compile_options(${frust_target} PRIVATE /FS /MP1)
        endif()
    endforeach()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin/Release")

juce_add_gui_app(DjehutiCAD
    PRODUCT_NAME "DjehutiCAD"
    VERSION 0.1.0
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/djehuti-cad-icon-256.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/djehuti-cad-icon-48.png"
)
set_target_properties(DjehutiCAD PROPERTIES OUTPUT_NAME "djehuti_cad")

target_sources(DjehutiCAD PRIVATE
    Source/Main.cpp
    Source/DjehutiCAD.h
    Source/DjehutiCAD.cpp
    Source/DjehutiMenuBarComponent.h
    Source/DjehutiMenuBarComponent.cpp
    Source/Auth/DesktopAuthSession.h
    Source/Auth/DesktopAuthSession.cpp
    Source/Auth/ProfileAvatarComponent.h
    Source/Auth/ProfileAvatarComponent.cpp
    Source/FrustEngine.h
    Source/FrustEngine.cpp
    Source/FrustExecution.h
    Source/FrustExecution.cpp
    Source/FrustPanel.h
    Source/FrustPanel.cpp
    Source/NodeCompiler.h
    Source/NodeCompiler.cpp
    Source/NodeDesignerPanel.h
    Source/NodeDesignerPanel.cpp
    Source/ProtectedCredentialStore.h
    Source/ProtectedCredentialStore.cpp
    Source/DjehutiAgent.h
    Source/DjehutiAgent.cpp
    Source/FrameResourceRing.h
    Source/Viewport3D.h
    Source/Viewport3D.cpp
    Source/CadDocument.h
    Source/CadDocument.cpp
    Source/CadMechanics.h
    Source/CadMechanics.cpp
    Source/CadMotionRuntime.h
    Source/CadMotionRuntime.cpp
    Source/MotionLink.h
    Source/MotionLink.cpp
    Source/CadWorkspacePanel.h
    Source/CadWorkspacePanel.cpp
)

target_compile_definitions(DjehutiCAD PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    FRUSTLANG_ROOT="${FRUSTLANG_ROOT}"
    DJEHUTICAD_APP=1
)
target_include_directories(DjehutiCAD PRIVATE Source)
if(EXISTS "${OCCT_INCLUDE_DIR}/TopoDS_Shape.hxx")
    target_include_directories(DjehutiCAD PRIVATE "${OCCT_INCLUDE_DIR}")
    target_compile_definitions(DjehutiCAD PRIVATE HAVE_OCCT=1 OCCT_STATIC)
    target_link_directories(DjehutiCAD PRIVATE
        "$<$<CONFIG:Debug>:${OCCT_ROOT}/win64/vc14/libd>"
        "$<$<NOT:$<CONFIG:Debug>>:${OCCT_LIBRARY_DIR}>")
endif()
if(MSVC)
    target_compile_options(DjehutiCAD PRIVATE /FS /MP1)
endif()
target_link_libraries(DjehutiCAD PRIVATE
    CreationDock
    ai_provider
    frust_plugin_host
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_opengl
)
if(WIN32)
    target_link_libraries(DjehutiCAD PRIVATE crypt32)
endif()

if(EXISTS "${OCCT_INCLUDE_DIR}/TopoDS_Shape.hxx")
    target_link_libraries(DjehutiCAD PRIVATE TKernel TKMath TKG2d TKG3d TKGeomBase TKGeomAlgo TKBRep TKTopAlgo TKPrim TKMesh TKShHealing TKBO TKBool TKCDF TKStd TKStdL TKLCAF TKCAF TKBin TKBinL)
endif()

juce_generate_juce_header(DjehutiCAD)
