# Post-build deploy: replace the plugin DLL in the Ashita plugins folder.
# Usage: cmake -DSRC=<built dll> -DDST_DIR=<plugins dir> -P deploy.cmake
# Never fails the build: if the DLL is locked (still loaded in game), it prints a warning instead.

get_filename_component(name "${SRC}" NAME)
set(dst "${DST_DIR}/${name}")

file(REMOVE "${dst}")
if (EXISTS "${dst}")
    message(WARNING "FarplaneUI deploy: ${dst} is in use (still loaded in game?). /unload FarplaneUI and rebuild to deploy.")
    return()
endif()

file(COPY_FILE "${SRC}" "${dst}" RESULT result)
if (result)
    message(WARNING "FarplaneUI deploy: copy to ${dst} failed: ${result}")
else()
    message(STATUS "FarplaneUI deployed to ${dst}")
endif()
