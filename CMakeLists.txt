cmake_minimum_required (VERSION 3.10)

project (mniam_player)

add_executable(mniam_player main.c amcom.c)
target_link_libraries(mniam_player Ws2_32.lib)