include_guard(GLOBAL)

function(orm_cxx_add_project_settings)
    if(TARGET orm-cxx-project-warnings)
        return()
    endif()

    add_library(orm-cxx-project-warnings INTERFACE)

    if(MSVC)
        target_compile_options(orm-cxx-project-warnings INTERFACE /W4 /permissive- /bigobj)

        if(ORM_CXX_WARNINGS_AS_ERRORS)
            target_compile_options(orm-cxx-project-warnings INTERFACE /WX)
        endif()
    else()
        target_compile_options(
            orm-cxx-project-warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat
        )

        if(ORM_CXX_WARNINGS_AS_ERRORS)
            target_compile_options(orm-cxx-project-warnings INTERFACE -Werror)
        endif()
    endif()
endfunction()
