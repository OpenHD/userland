set(EXEC hello_video.bin)
set(SRCS video.cpp)

add_executable(${EXEC} ${SRCS})
target_link_libraries(${EXEC} ${HELLO_PI_LIBS})

add_executable(fpv_video1 fpv_video1.cpp)
target_link_libraries(fpv_video1 ${HELLO_PI_LIBS})


install(TARGETS ${EXEC}
        RUNTIME DESTINATION bin)
