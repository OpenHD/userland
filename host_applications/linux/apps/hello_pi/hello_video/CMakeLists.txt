set(EXEC video.bin)
set(SRCS video.c)

add_executable(${EXEC} ${SRCS})
target_link_libraries(${EXEC} ${HELLO_PI_LIBS})

add_compile_options(-Wno-write-strings)
add_compile_options(-Wno-unused-variable)

add_executable(fpv_video0.bin fpv_video0.cpp)
target_link_libraries(fpv_video0.bin ${HELLO_PI_LIBS})

#add_executable(fpv_video1.bin fpv_video1.cpp)
#target_link_libraries(fpv_video1.bin ${HELLO_PI_LIBS})

#add_executable(fpv_video2.bin fpv_video2.cpp)
#target_link_libraries(fpv_video2.bin ${HELLO_PI_LIBS})

install(TARGETS ${EXEC}
        RUNTIME DESTINATION bin)

install(TARGETS fpv_video0.bin
        RUNTIME DESTINATION bin)

#install(TARGETS fpv_video1.bin
#        RUNTIME DESTINATION bin)

#install(TARGETS fpv_video2.bin
#        RUNTIME DESTINATION bin)
