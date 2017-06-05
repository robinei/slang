cmake_minimum_required(VERSION 2.6.0)

project(slang C)

add_executable(main main.c rt_gc.c rt_gettype.c rt_print.c rt_types.c)

