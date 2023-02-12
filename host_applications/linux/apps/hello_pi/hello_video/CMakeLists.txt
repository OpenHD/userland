set(EXEC hello_video.bin)
set(SRCS video.cpp)

add_executable(${EXEC} ${SRCS})
target_link_libraries(${EXEC} ${HELLO_PI_LIBS})

install(TARGETS ${EXEC}
        RUNTIME DESTINATION bin)
