cmake_minimum_required(VERSION 2.6.0)

project(slang C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Wno-unused-function")

set(RuntimeSources
    murmur3.c
    rt_eval.c
    rt_gc.c
    rt_gettype.c
    rt_parse.c
    rt_primops.c
    rt_print.c
    rt_read.c
    rt.c
    strtod.c
    strtoll.c
    )

add_library(runtime STATIC ${RuntimeSources})

add_executable(main main.c)
target_link_libraries(main runtime)

add_executable(runtests test/runtests.c test/test_gc.c)
target_include_directories(runtests PRIVATE .)
target_link_libraries(runtests runtime)
