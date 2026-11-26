# PCSX2 PS4 port (AI-assisted): links Makefile.ps4's objects into eboot.bin with CEMUPS4's PS4 runtime,
# CRT, link script, RADV (Mesa on GNM) and glslang. See ps4/build-ps4.sh. Needs proper testing on the console.
cmake_minimum_required(VERSION 3.13)
project(pcsx2_ps4 C CXX ASM)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

set(PS4_PORT_DIR "$ENV{HOME}/cemups4" CACHE PATH "CEMUPS4 checkout (toolchain/ and port/)")
include(${PS4_PORT_DIR}/toolchain/ps4-love-style.cmake)

set(PCSX2_OBJECTS_FILE "" CACHE FILEPATH "objects.txt written by Makefile.ps4")
if (NOT EXISTS "${PCSX2_OBJECTS_FILE}")
    message(FATAL_ERROR "PCSX2_OBJECTS_FILE: run make -f Makefile.ps4 objects.txt first")
endif ()
file(STRINGS ${PCSX2_OBJECTS_FILE} pcsx2_objects)
set_source_files_properties(${pcsx2_objects} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

add_executable(pcsx2 ${PS4_RUNTIME_SOURCES} ${pcsx2_objects})
set_target_properties(pcsx2 PROPERTIES LINKER_LANGUAGE CXX)

# Performance experiment inputs (build-ps4.sh passes them every time, so a changed knob reaches this cached build).
set(PS4_NARROW "1" CACHE STRING "1 when Makefile.ps4 blocked LLVM's narrow store optimization")
set(PS4_LTO "0" CACHE STRING "1 when Makefile.ps4 compiled the objects with -flto=thin")
if (DEFINED ENV{ORBIS_ORDER})
    set(_orbis_order_default "$ENV{ORBIS_ORDER}")
else ()
    get_filename_component(_orbis_order_default "${CMAKE_CURRENT_SOURCE_DIR}/../orbis-ps4-hot.order" ABSOLUTE)
endif ()
set(ORBIS_ORDER_FILE "${_orbis_order_default}" CACHE STRING "ld.lld --symbol-ordering-file, or 'none'")

# ps4.cmake links with ld.lld directly, not through clang's driver. clang's -flto=thin is a driver flag and is not
# valid here; ld.lld reads Makefile.ps4's ThinLTO bitcode objects and runs their ThinLTO backends itself. That
# ld.lld must be new enough to read clang-21's bitcode (normally the LLVM 21 ld.lld next to it). Compile the
# runtime sources the same way and keep the backend at the compile flags' O3. Needs proper testing: LTO can change
# both performance and the order file's effect.
if (PS4_LTO STREQUAL "1")
    # ps4.cmake selects OpenOrbis' LLVM 12 ld.lld, which cannot read clang-21 ThinLTO bitcode.
    # Use the linker clang-21 itself names; fail configure rather than dying in the final link.
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-prog-name=ld.lld
                    OUTPUT_VARIABLE _ps4_lto_linker OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT EXISTS "${_ps4_lto_linker}")
        message(FATAL_ERROR "PS4_LTO=1 needs the ld.lld next to ${CMAKE_CXX_COMPILER}")
    endif ()
    set(CMAKE_LINKER "${_ps4_lto_linker}" CACHE FILEPATH "linker used for clang-21 ThinLTO bitcode" FORCE)
    message(STATUS "ThinLTO linker: ${CMAKE_LINKER}")
    target_compile_options(pcsx2 PRIVATE -flto=thin)
    string(APPEND CMAKE_EXE_LINKER_FLAGS " --lto-O3 -z norelro") # LLVM 21 otherwise splits RELRO into a second RW PT_LOAD, which create-fself cannot represent safely
    # Without LTO this reaches the backend while each object is compiled. With LTO the backend runs here instead,
    # and clang does not encode that -mllvm answer in the bitcode, so ld.lld needs it again.
    if (PS4_NARROW STREQUAL "1")
        string(APPEND CMAKE_EXE_LINKER_FLAGS " -mllvm -combiner-reduce-load-op-store-width=false")
    endif ()
    message(STATUS "ThinLTO: bitcode objects, ld.lld backend at O3")
endif ()

# As ps5/coreorbis/link-vk.sh: hot functions first in .text (lld --symbol-ordering-file). ORBIS_ORDER=none disables
# it; another file can be named without editing the build. Makefile.ps4's C++ objects are compiled with
# -ffunction-sections, the sections this reorders.
if (NOT ORBIS_ORDER_FILE STREQUAL "none")
    if (EXISTS "${ORBIS_ORDER_FILE}")
        string(APPEND CMAKE_EXE_LINKER_FLAGS " --symbol-ordering-file=${ORBIS_ORDER_FILE} --no-warn-symbol-ordering")
        set_property(TARGET pcsx2 APPEND PROPERTY LINK_DEPENDS "${ORBIS_ORDER_FILE}")
        message(STATUS "function order: ${ORBIS_ORDER_FILE}")
    else ()
        message(WARNING "ORBIS_ORDER_FILE does not exist, linking without it: ${ORBIS_ORDER_FILE}")
    endif ()
else ()
    message(STATUS "function order: disabled")
endif ()

# Deliberately no --gc-sections. PS5SX2 uses function/data sections for hot-function ordering in its main eboot but
# does not garbage collect it. This binary has .incbin resource sections and constructor ordering requirements, and
# losing one the console side expects is worse than the unmeasured size win; follow that conservative link until a
# PS4 test proves GC safe.
# The Vulkan window system on VideoOut (ps4/ps4_vk_wsi.cpp): PCSX2's and the frontend's calls to RADV's entry point go
# through it.
string(APPEND CMAKE_EXE_LINKER_FLAGS " --wrap=vk_icdGetInstanceProcAddr")
string(APPEND CMAKE_EXE_LINKER_FLAGS " --script ${CMAKE_CURRENT_SOURCE_DIR}/orbis-text.ld")
# PS5SX2's weak references to the PS5 driver (ps5vk), checked for null before use: null here, as in PS5SX2's own RADV
# eboot (create-fself refuses a weak name nothing defines).
foreach (sym orbis_ps5vk_war_shim ps5vk_debug_threads ps5vk_debug_timestamp_flush ps5vk_output_refresh_millihertz
         ps5vk_output_restore_default ps5vk_output_status
         ps4_jit_owner ps4_jit_dump_function) # CEMUPS4 port/ps4_sampler.cpp: Cemu's JIT lookups, weak
    string(APPEND CMAKE_EXE_LINKER_FLAGS " --defsym=${sym}=0")
endforeach ()
# CEMUPS4's resolver (port/ps4_net.cpp) falls back to the C library's lookups through __real_; ld.lld doesn't take a
# library member for a name reached only that way, so ask for them (create-fself: "missing module for gethostbyname").
string(APPEND CMAKE_EXE_LINKER_FLAGS " -u gethostbyname -u getaddrinfo")
# Every undefined name at once (the PS5-only calls), not the first 20.
string(APPEND CMAKE_EXE_LINKER_FLAGS " --error-limit=0")

ps4_link_mesa(pcsx2)  # RADV on GNM
# paraLLEl-GS and the Granite libraries under it (ps4/build-pgs.sh; Makefile.ps4 PGS=1), when they were built.
set(PGS_LIBS_FILE "$ENV{HOME}/pcsx2ps4-build/pgs/libs.txt" CACHE FILEPATH "libs.txt written by ps4/build-pgs.sh")
if (EXISTS "${PGS_LIBS_FILE}")
    file(STRINGS ${PGS_LIBS_FILE} pgs_libs)
    message(STATUS "paraLLEl-GS libraries: ${pgs_libs}")
    target_link_libraries(pcsx2 PRIVATE ${pgs_libs}) # ld.lld directly, inside the link's --start-group
endif ()
target_link_libraries(pcsx2 PRIVATE
                      ${PS4_SYSROOT}/lib/libglslang.a
                      ${PS4_SYSROOT}/lib/libglslang-default-resource-limits.a
                      ${PS4_SYSROOT}/lib/libz.a
                      ${PS4_SYSROOT}/lib/libsmb2.a
                      SceSysmodule SceSystemService SceUserService SceVideoOut SceAudioOut ScePad SceNet SceNetCtl
                      SceRandom SceImeDialog SceCommonDialog SceMouse SceKeyboard pthread)
ps4_add_eboot(pcsx2)
