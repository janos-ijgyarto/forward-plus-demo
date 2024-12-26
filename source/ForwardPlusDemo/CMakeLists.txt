add_subdirectory(Application)
add_subdirectory(GraphicsAPI)
add_subdirectory(Render)
add_subdirectory(Utilities)

target_sources(${FORWARDPLUSDEMO_CURRENT_TARGET}
    PRIVATE
    main.cpp
   )