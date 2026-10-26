cmake_minimum_required(VERSION 3.20)
project(FirmGuard VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CTest)

add_library(firmguard src/parse.c)
add_library(FirmGuard::firmguard ALIAS firmguard)
target_compile_features(firmguard PUBLIC c_std_11)
target_include_directories(firmguard PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(firmguard PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 0
  WINDOWS_EXPORT_ALL_SYMBOLS ON)
if(MSVC)
  target_compile_options(firmguard PRIVATE /W4 /WX /GS /sdl)
else()
  target_compile_options(firmguard PRIVATE -Wall -Wextra -Wpedantic -Werror
    -fstack-protector-strong)
endif()

add_executable(fg-inspect cli/main.c)
target_link_libraries(fg-inspect PRIVATE firmguard)
target_include_directories(fg-inspect PRIVATE cli)
if(MSVC)
  target_compile_options(fg-inspect PRIVATE /W4 /WX /GS /sdl)
else()
  target_compile_options(fg-inspect PRIVATE -Wall -Wextra -Wpedantic -Werror
    -fstack-protector-strong)
endif()

if(BUILD_TESTING)
  if(UNIX)
    find_package(Threads REQUIRED)
    add_executable(test-parser tests/test_parser.c)
    target_link_libraries(test-parser PRIVATE firmguard Threads::Threads)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_definitions(test-parser PRIVATE FG_ALLOCATION_WRAP)
      target_link_options(test-parser PRIVATE -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc)
    endif()
    if(MSVC)
      target_compile_options(test-parser PRIVATE /W4 /WX /GS /sdl)
    else()
    target_compile_options(test-parser PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME parser-contract COMMAND test-parser)
  else()
    message(STATUS "POSIX C contract test skipped; Rust tests run on all platforms")
  endif()
endif()

install(TARGETS firmguard fg-inspect EXPORT FirmGuardTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES include/firmguard.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT FirmGuardTargets NAMESPACE FirmGuard::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FirmGuard)
