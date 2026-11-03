####################################################################################################
# Farplane UI - Ashita v4 Plugin
####################################################################################################

cmake_minimum_required(VERSION 3.22)

set(ASHITA_PROJECT_NAME "FarplaneUI")
project(${ASHITA_PROJECT_NAME} CXX)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if (NOT CMAKE_BUILD_PARALLEL_LEVEL)
    include(ProcessorCount)
    ProcessorCount(N)
    if (NOT N EQUAL 0)
        set(CMAKE_BUILD_PARALLEL_LEVEL ${N})
    endif()
endif()

find_package(AshitaSDK REQUIRED)
ashita_sdk_initialize()

set(${ASHITA_PROJECT_NAME}_res
    )

set(${ASHITA_PROJECT_NAME}_src
    "src/exports.def"
    "src/farplaneui.cpp"
    "src/farplaneui.hpp"
    "src/mask.cpp"
    "src/mask.hpp"
    "src/settings.cpp"
    "src/settings.hpp"
    "src/theme.hpp"
    "src/cfgui.cpp"
    "src/cfgui.hpp"
    "src/data/party_recasts.hpp"
    "src/lsgate.hpp"
    "src/mobdb.cpp"
    "src/mobdb.hpp"
    "src/worldring.cpp"
    "src/worldring.hpp"
    "src/module.hpp"
    "src/ui.cpp"
    "src/modules.hpp"
    "src/chatparse.cpp"
    "src/chatparse.hpp"
    "src/ember.cpp"
    "src/data/quest_db.hpp"
    "src/modules/mod_quests.cpp"
    "src/ember.hpp"
    "src/modules/bars.cpp"
    "src/modules/common.cpp"
    "src/modules/common.hpp"
    "src/modules/mod_mask.cpp"
    "src/modules/mod_party.cpp"
    "src/modules/mod_chat.cpp"
    "src/modules/mod_minimap.cpp"
    "src/modules/mod_inventory.cpp"
    "src/modules/mod_equip.cpp"
    "src/modules/mod_native.cpp"
    "src/modules/mod_target.cpp"
    "src/modules/mod_targetbar.cpp"
    "src/modules/mod_points.cpp"
    "src/modules/mod_ticks.cpp"
    "src/modules/mod_notify.cpp"
    "src/modules/mod_recast.cpp"
    "src/modules/mod_castbar.cpp"
    "src/modules/mod_status.cpp"
    "src/modules/mod_enemy.cpp"
    "src/modules/mod_readycheck.cpp"
    "src/modules/mod_alliance.cpp"
    "src/modules/mod_compass.cpp"
    "src/modules/mod_cmdmenu.cpp"
    "src/modules/mod_dps.cpp"
    )

add_library(${ASHITA_PROJECT_NAME} MODULE ${${ASHITA_PROJECT_NAME}_res} ${${ASHITA_PROJECT_NAME}_src})
set_target_properties(${ASHITA_PROJECT_NAME} PROPERTIES
    CXX_EXTENSIONS ON
    CXX_STANDARD_REQUIRED ON
    LINKER_LANGUAGE CXX
    )

if (WIN32)
    set_target_properties(${ASHITA_PROJECT_NAME} PROPERTIES
        OUTPUT_NAME ${ASHITA_PROJECT_NAME}
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
        )
endif()

ashita_sdk_attach(${ASHITA_PROJECT_NAME})
target_compile_definitions(${ASHITA_PROJECT_NAME} PRIVATE NOMINMAX)
if (MSVC)
    # Sources are ASCII; /utf-8 keeps MSVC from raising C4819 (an error under /WX) on non-UTF-8 code pages.
    target_compile_options(${ASHITA_PROJECT_NAME} PRIVATE /utf-8)
endif()
ashita_sdk_set_compiler_defaults(${ASHITA_PROJECT_NAME})
ashita_sdk_set_compiler_warn_level(${ASHITA_PROJECT_NAME} 4)
ashita_sdk_set_compiler_warnings_as_errors(${ASHITA_PROJECT_NAME})
ashita_sdk_set_linker_defaults(${ASHITA_PROJECT_NAME})

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    ashita_sdk_set_linker_enable_debug_linking(${ASHITA_PROJECT_NAME})
else()
    ashita_sdk_set_linker_disable_debug_linking(${ASHITA_PROJECT_NAME})
endif()

# SafeSEH stays OFF: the official example notes it must be disabled when calling Direct3D/D3DX
# functions (the SDK's d3dx8.lib has no SafeSEH tables, so /SAFESEH would fail with LNK2026).
# The mask uses D3DXCreateSprite / D3DXCreateTextureFromFileExA.
target_link_options(${ASHITA_PROJECT_NAME} PRIVATE /SAFESEH:NO)

# Replace the DLL in the game's plugins folder after every build (empty or missing folder = off).
set(FARPLANEUI_DEPLOY_DIR "C:/PhoenixXI/plugins" CACHE PATH "Ashita plugins folder to deploy the built DLL to")
if (FARPLANEUI_DEPLOY_DIR AND EXISTS "${FARPLANEUI_DEPLOY_DIR}")
    add_custom_command(TARGET ${ASHITA_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DSRC=$<TARGET_FILE:${ASHITA_PROJECT_NAME}> -DDST_DIR=${FARPLANEUI_DEPLOY_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/deploy.cmake
        VERBATIM)
endif()
