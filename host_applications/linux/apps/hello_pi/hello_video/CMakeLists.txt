set(EXEC video.bin)
set(SRCS video.c)

add_executable(${EXEC} ${SRCS})
target_link_libraries(${EXEC} ${HELLO_PI_LIBS})

add_compile_options(-Wno-write-strings)
add_compile_options(-Wno-unused-variable)

add_executable(fpv_video0.bin fpv_video0.cpp)
set_target_properties(fpv_video0.bin PROPERTIES OUTPUT_NAME openhd_pi_vid)
target_link_libraries(fpv_video0.bin ${HELLO_PI_LIBS})

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(FPV_HEVC QUIET libavcodec libavformat libavutil libdrm)
    find_library(FPV_MMAL NAMES libmmal.so.0)
    find_library(FPV_MMAL_CORE NAMES libmmal_core.so.0)
    find_library(FPV_MMAL_UTIL NAMES libmmal_util.so.0)
    find_library(FPV_VCSM NAMES libvcsm.so.0)
    if(FPV_HEVC_FOUND AND FPV_MMAL AND FPV_MMAL_CORE AND FPV_MMAL_UTIL AND FPV_VCSM)
        target_sources(fpv_video0.bin PRIVATE fpv_hevc.cpp)
        target_compile_definitions(fpv_video0.bin PRIVATE FPV_HEVC)
        target_include_directories(fpv_video0.bin PRIVATE ${FPV_HEVC_INCLUDE_DIRS})
        target_link_libraries(fpv_video0.bin ${FPV_HEVC_LIBRARIES}
            ${FPV_MMAL} ${FPV_MMAL_CORE} ${FPV_MMAL_UTIL} ${FPV_VCSM})
    endif()
endif()

#add_executable(fpv_video1.bin fpv_video1.cpp)
#target_link_libraries(fpv_video1.bin ${HELLO_PI_LIBS})

#add_executable(fpv_video2.bin fpv_video2.cpp)
#target_link_libraries(fpv_video2.bin ${HELLO_PI_LIBS})

install(TARGETS ${EXEC}
        RUNTIME DESTINATION bin)

install(TARGETS fpv_video0.bin
        RUNTIME DESTINATION bin)
install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink openhd_pi_vid \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/fpv_video0.bin\")")

#install(TARGETS fpv_video1.bin
#        RUNTIME DESTINATION bin)

#install(TARGETS fpv_video2.bin
#        RUNTIME DESTINATION bin)
