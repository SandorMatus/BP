idf_component_register(SRCS "mesh_main.c"
                           
                    INCLUDE_DIRS "." "include")
component_compile_options(-Wno-error=format= -Wno-format)
