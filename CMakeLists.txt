cmake_minimum_required(VERSION 3.10)

project(delcore30m-tests C)

find_package(PkgConfig)
pkg_check_modules(LibDRM REQUIRED IMPORTED_TARGET libdrm)

if(NOT ELCORE30M_TOOLCHAIN_FILE)
    message(FATAL_ERROR "ELcore-30M toolchain file is not specified")
endif()

function(elcore30m_load TOOLCHAIN_FILE)
    include(${TOOLCHAIN_FILE})
    set(ELCORE30M_PREFIX ${CMAKE_TOOLCHAIN_PREFIX} PARENT_SCOPE)
    set(ELCORE30M_CC ${CMAKE_C_COMPILER} PARENT_SCOPE)
    set(ELCORE30M_CROSS_COMPILE ${CMAKE_TOOLCHAIN_PREFIX}/bin/${TRIPLE}-)
    set(ELCORE30M_OBJCOPY ${ELCORE30M_CROSS_COMPILE}objcopy PARENT_SCOPE)
endfunction()

elcore30m_load(${ELCORE30M_TOOLCHAIN_FILE})

# ELCORE30M toolchain should be at beginning of PATH to prevent use clang
# from system.
set(PATH ${ELCORE30M_PREFIX}/bin:$$PATH)

set(ELCORE30M_C_FLAGS -target elcore -mcpu=elcore30m -nostartfiles -Wa,-mcx7)
set(ELCORE30M_OBJCOPY_FLAGS --set-section-flags .bss=alloc,load,contents)

set(ELCORE30M_ASM_SOURCE
    fibonacci.s
    inverse-demo.s
    inversiontest.s
    inversiontest-profile.s
)

set(FIRMWARE_INSTALL_PATH share/delcore30m-tests)

foreach(source IN LISTS ELCORE30M_ASM_SOURCE)
    string(REPLACE ".s" "" base ${source})
    set(elf "${base}.fw.elf")
    set(bin "${base}.fw.bin")

    add_custom_command(OUTPUT ${bin}
        COMMAND env PATH=${PATH} ${ELCORE30M_CC} ${ELCORE30M_C_FLAGS}
                ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${elf}
        COMMAND ${ELCORE30M_OBJCOPY} ${ELCORE30M_OBJCOPY_FLAGS} -O binary ${elf}
                ${bin}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source}
    )

    add_custom_target(${elf} ALL DEPENDS
                  ${CMAKE_CURRENT_BINARY_DIR}/${bin})

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${bin} DESTINATION ${FIRMWARE_INSTALL_PATH})
endforeach()

set(ELCORE30M_C_SOURCE
    detector.c
    sum.c
)

foreach(source IN LISTS ELCORE30M_C_SOURCE)
    string(REPLACE ".c" "" base ${source})
    set(elf "${base}.fw.elf")
    set(bin "${base}.fw.bin")
    set(crt "crt0-${base}.s")

    add_custom_command(OUTPUT ${bin}
        COMMAND env PATH=${PATH} ${ELCORE30M_CC} ${ELCORE30M_C_FLAGS}
                ${CMAKE_CURRENT_SOURCE_DIR}/${crt} ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${elf}
        COMMAND ${ELCORE30M_OBJCOPY} ${ELCORE30M_OBJCOPY_FLAGS} -O binary ${elf}
                ${bin}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source}
    )

    add_custom_target(${elf} ALL DEPENDS
                  ${CMAKE_CURRENT_BINARY_DIR}/${bin})

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${bin}  DESTINATION ${FIRMWARE_INSTALL_PATH})
endforeach()

add_definitions(-DFIRMWARE_PATH=${CMAKE_INSTALL_PREFIX}/${FIRMWARE_INSTALL_PATH}/)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")

add_executable(delcore30m-cpudetector delcore30m-cpudetector.c drmdisplay.c dspinverse.c stbfont.c)
add_executable(delcore30m-dspdetector delcore30m-dspdetector.c drmdisplay.c dspdetector.c stbfont.c)
add_executable(delcore30m-fibonacci delcore30m-fibonacci.c)
add_executable(delcore30m-inversiondemo delcore30m-inversiondemo.c drmdisplay.c dspinverse.c
                                        stbfont.c)
add_executable(delcore30m-inversiontest delcore30m-inversiontest.c)
add_executable(delcore30m-paralleltest delcore30m-paralleltest.c)

target_link_libraries(delcore30m-cpudetector PkgConfig::LibDRM m pthread)
target_link_libraries(delcore30m-dspdetector PkgConfig::LibDRM m pthread)
target_link_libraries(delcore30m-inversiondemo PkgConfig::LibDRM m pthread)
target_link_libraries(delcore30m-inversiontest m)
target_link_libraries(delcore30m-paralleltest pthread)

install(TARGETS delcore30m-cpudetector delcore30m-dspdetector delcore30m-fibonacci
                delcore30m-inversiondemo delcore30m-inversiontest delcore30m-paralleltest
        RUNTIME DESTINATION bin)
install(PROGRAMS delcore30m-test.py DESTINATION bin)
