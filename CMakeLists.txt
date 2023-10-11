cmake_minimum_required(VERSION 3.21)

if(${CMAKE_CURRENT_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_BINARY_DIR})
  message(FATAL_ERROR "In-tree build attempted, aborting build.")
endif()

project(ForwardPlusDemo
        VERSION 1.0.0
        DESCRIPTION "Forward+ lighting demo app (with CMake)"
        LANGUAGES C CXX)
		
set(FORWARDPLUSDEMO_CURRENT_TARGET "ForwardPlusDemo")

add_executable(${FORWARDPLUSDEMO_CURRENT_TARGET} WIN32)

get_property("FORWARDPLUSDEMO_SOURCES" TARGET ${FORWARDPLUSDEMO_CURRENT_TARGET} PROPERTY SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${FORWARDPLUSDEMO_SOURCES})

# Limit to C++11
target_compile_features(${FORWARDPLUSDEMO_CURRENT_TARGET} PRIVATE cxx_std_11)
target_include_directories(${FORWARDPLUSDEMO_CURRENT_TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source")

# Link DirectX-related libraries
target_link_libraries(${FORWARDPLUSDEMO_CURRENT_TARGET} PRIVATE d3d11 dxgi dxguid d3dcompiler)

if(MSVC)
  target_compile_options(${FORWARDPLUSDEMO_CURRENT_TARGET} PRIVATE /W4 /WX)
endif()

# FIXME: this may be unnecessary, could just use the runtime output dir?
if(WIN32)
	if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
			set_target_properties(${FORWARDPLUSDEMO_CURRENT_TARGET}
					PROPERTIES 
					RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/debug/${PROJECT_NAME}"
					RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/release/${PROJECT_NAME}"
					LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/debug/${PROJECT_NAME}"
					LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/release/${PROJECT_NAME}"
			)
	endif()
endif()

add_subdirectory(source)

if(MSVC OR ${CMAKE_GENERATOR} MATCHES "Xcode")
	set_property(GLOBAL PROPERTY USE_FOLDERS ON)
	set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake Targets")
endif()