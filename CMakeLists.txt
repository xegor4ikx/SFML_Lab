cmake_minimum_required(VERSION 3.20)

project(SFML_Lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SFML підключається через vcpkg (див. vcpkg.json і CMakePresets.json)
find_package(SFML COMPONENTS Graphics Window System CONFIG REQUIRED)

add_executable(SFML_Lab
  main.cpp          # вікно, введення, відображення
  physics.h
  physics.cpp       # фізична модель
  experiment.cpp    # обчислювальний експеримент (--experiment)
)

target_link_libraries(SFML_Lab PRIVATE
  SFML::Graphics
  SFML::Window
  SFML::System
)

# Вихідні файли збережено в UTF-8 (українські написи у вікні)
if(MSVC)
  target_compile_options(SFML_Lab PRIVATE /utf-8)
else()
  target_compile_options(SFML_Lab PRIVATE -Wall -Wextra)
endif()

# Копіюємо папку resources (шрифт) поруч із виконуваним файлом
add_custom_command(TARGET SFML_Lab POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/resources
          $<TARGET_FILE_DIR:SFML_Lab>/resources
)
