set(EXEC hello_video.bin)
set(SRCS video.cpp)

add_executable(${EXEC} ${SRCS})
target_link_libraries(${EXEC} ${HELLO_PI_LIBS})

add_executable(fpv_video1.bin fpv_video1.cpp)
target_link_libraries(fpv_video1.bin ${HELLO_PI_LIBS})


install(TARGETS ${EXEC}
        RUNTIME DESTINATION bin)

install(TARGETS fpv_video1.bin
        RUNTIME DESTINATION bin)
