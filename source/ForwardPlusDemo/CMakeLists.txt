add_subdirectory(Application)
add_subdirectory(GraphicsAPI)
add_subdirectory(Render)

target_sources(${FORWARDPLUSDEMO_CURRENT_TARGET}
    PRIVATE
    main.cpp
   )