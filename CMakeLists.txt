cmake_minimum_required(VERSION 3.15)

project(sekai-deck-recommend-cpp VERSION 0.3.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/3rdparty/yyjson/src)

file(GLOB_RECURSE SRC_FILES "${PROJECT_SOURCE_DIR}/src/*.cpp")
set(YYJSON_SOURCE "${PROJECT_SOURCE_DIR}/3rdparty/yyjson/src/yyjson.c")

# Each binding file owns its own toolchain-specific binding macro
# (PYBIND11_MODULE vs EMSCRIPTEN_BINDINGS). Strip both from the engine glob
# and link the right one per target below.
list(REMOVE_ITEM SRC_FILES
    "${PROJECT_SOURCE_DIR}/src/sekai_deck_recommend.cpp"
    "${PROJECT_SOURCE_DIR}/src/sekai_deck_recommend_wasm.cpp"
)

# 浏览器端引擎内并行（需要站点开启COOP/COEP以获得SharedArrayBuffer）；
# 默认OFF，产物与历史一致
option(SEKAI_WASM_THREADS "Build wasm with pthreads for in-engine parallelism" OFF)

if (EMSCRIPTEN)
    add_executable(sekai_deck_recommend
        ${SRC_FILES}
        ${YYJSON_SOURCE}
        "${PROJECT_SOURCE_DIR}/src/sekai_deck_recommend_wasm.cpp"
    )
    target_compile_options(sekai_deck_recommend PRIVATE
        -Wall -Wextra -Wuninitialized
        -fwasm-exceptions
    )
    if (SEKAI_WASM_THREADS)
        target_compile_options(sekai_deck_recommend PRIVATE -pthread)
        target_link_options(sekai_deck_recommend PRIVATE
            -pthread
            "SHELL:-s PTHREAD_POOL_SIZE=navigator.hardwareConcurrency"
        )
    endif()
    target_link_options(sekai_deck_recommend PRIVATE
        -fwasm-exceptions
        -lembind
        "SHELL:-s MODULARIZE=1"
        "SHELL:-s EXPORT_ES6=1"
        "SHELL:-s EXPORT_NAME=createSekaiDeckRecommendModule"
        "SHELL:-s ENVIRONMENT=web,worker"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s INITIAL_MEMORY=64MB"
        "SHELL:-s MAXIMUM_MEMORY=2GB"
        "SHELL:-s STACK_SIZE=4MB"
        "SHELL:-s FILESYSTEM=1"
        "SHELL:-s WASM=1"
        "SHELL:-s ALLOW_TABLE_GROWTH=1"
        "SHELL:--embed-file ${PROJECT_SOURCE_DIR}/data@/data"
    )
    set_target_properties(sekai_deck_recommend PROPERTIES
        OUTPUT_NAME "sekai_deck_recommend"
        SUFFIX ".js"
    )
else()
    add_subdirectory(3rdparty/pybind11)

    pybind11_add_module(sekai_deck_recommend
        ${SRC_FILES}
        ${YYJSON_SOURCE}
        "${PROJECT_SOURCE_DIR}/src/sekai_deck_recommend.cpp"
    )

    if (MSVC)
        target_compile_options(sekai_deck_recommend PRIVATE
            /W4
            /MP
            /Z7
        )
        target_link_options(sekai_deck_recommend PRIVATE
            /DEBUG:NONE
        )
    else()
        target_compile_options(sekai_deck_recommend PRIVATE
            -Wall -Wextra -Wuninitialized
        )
    endif()

    # Release构建启用跨编译单元优化（实测低个位数百分比收益，构建耗时增加可接受）
    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        include(CheckIPOSupported)
        check_ipo_supported(RESULT ipo_supported OUTPUT ipo_check_output)
        if (ipo_supported)
            set_property(TARGET sekai_deck_recommend PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        endif()
    endif()

    set(INSTALL_DIR "sekai_deck_recommend_cpp")

    install(TARGETS sekai_deck_recommend DESTINATION ${INSTALL_DIR})
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/data" DESTINATION ${INSTALL_DIR})
    install(FILES "${PROJECT_SOURCE_DIR}/src/sekai_deck_recommend.pyi" DESTINATION ${INSTALL_DIR})
    install(FILES "${PROJECT_SOURCE_DIR}/src/__init__.py" DESTINATION ${INSTALL_DIR})
endif()
