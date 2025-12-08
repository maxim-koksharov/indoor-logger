# my_cpp_project/CMakeLists.txt

# Указываем минимальную версию CMake
cmake_minimum_required(VERSION 3.16)

# Включаем инструментарий SDK (обязательный шаг)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Создаем проект
project(my_cpp_rtos_project)
