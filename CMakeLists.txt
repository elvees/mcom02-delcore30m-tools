cmake_minimum_required(VERSION 3.0)

project(delcore30m-tests C)

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

set(ELCORE30M_C_FLAGS -target elcore -mcpu=elcore30m -nostartfiles -Wa,-mcx7)
set(ELCORE30M_OBJCOPY_FLAGS --set-section-flags .bss=alloc,load,contents)

set(ELCORE30M_ASM_SOURCE
    fibonacci.s
)

set(FIRMWARE_INSTALL_PATH share/delcore30m-tests)

foreach(source IN LISTS ELCORE30M_ASM_SOURCE)
    string(REPLACE ".s" "" base ${source})
    set(elf "${base}.fw.elf")
    set(bin "${base}.fw.bin")

    add_custom_command(OUTPUT ${bin}
        COMMAND env PATH=${ELCORE30M_PREFIX}/bin:$$PATH ${ELCORE30M_CC} ${ELCORE30M_C_FLAGS}
                ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${elf}
        COMMAND ${ELCORE30M_OBJCOPY} ${ELCORE30M_OBJCOPY_FLAGS} -O binary ${elf}
                ${bin}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source}
    )

    add_custom_target(${elf} ALL DEPENDS
                  ${CMAKE_CURRENT_BINARY_DIR}/${bin})

    install(FILES ${bin} DESTINATION ${FIRMWARE_INSTALL_PATH})
endforeach()

add_definitions(-DFIRMWARE_PATH=${CMAKE_INSTALL_PREFIX}/${FIRMWARE_INSTALL_PATH}/)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")

add_executable(delcore30m-fibonacci delcore30m-fibonacci.c)

install(TARGETS delcore30m-fibonacci RUNTIME DESTINATION bin)
