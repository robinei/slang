cmake_minimum_required(VERSION 2.6.0)

project(slang C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wall -Wno-unused-function")

add_executable(main main.c murmur3.c rt_eval.c rt_gc.c rt_gettype.c rt_parse.c rt_primops.c rt_print.c rt_read.c rt_types.c strtod.c strtoll.c)

