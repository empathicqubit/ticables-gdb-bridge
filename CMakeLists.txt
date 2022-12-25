project(tibridge)

cmake_minimum_required(VERSION 3.1.0)

find_package(PkgConfig REQUIRED)

PKG_CHECK_MODULES(GLIB REQUIRED glib-2.0)
PKG_CHECK_MODULES(TICABLES REQUIRED ticables2)
PKG_CHECK_MODULES(TICALCS REQUIRED ticalcs2)

file(GLOB COMMON_SRC src/common/*.c)

file(GLOB TIBRIDGE_SRC src/tibridge.c)

add_executable(tibridge ${COMMON_SRC} ${TIBRIDGE_SRC})

target_compile_options(tibridge PRIVATE -Werror)

target_include_directories(tibridge
	PRIVATE src/include/
	PRIVATE ${GLIB_INCLUDE_DIRS}
	PRIVATE ${TICABLES_INCLUDE_DIRS}
)

target_link_libraries(tibridge PRIVATE ${GLIB_LIBRARIES})
target_link_libraries(tibridge PRIVATE ${TICABLES_LIBRARIES})

target_link_directories(tibridge PRIVATE ${GLIB_LIBRARY_DIRS})
target_link_directories(tibridge PRIVATE ${TICABLES_LIBRARIES})

PKG_CHECK_MODULES(READLINE REQUIRED readline)

file(GLOB TIKEYS_SRC src/tikeys.c)

add_executable(tikeys ${COMMON_SRC} ${TIKEYS_SRC})

target_compile_options(tikeys PRIVATE -Werror)

target_include_directories(tikeys
	PRIVATE src/include/
	PRIVATE ${GLIB_INCLUDE_DIRS}
	PRIVATE ${TICABLES_INCLUDE_DIRS}
	PRIVATE ${TICALCS_INCLUDE_DIRS}
	PRIVATE ${READLINE_INCLUDE_DIRS}
)

target_link_libraries(tikeys PRIVATE ${GLIB_LIBRARIES})
target_link_libraries(tikeys PRIVATE ${TICABLES_LIBRARIES})
target_link_libraries(tikeys PRIVATE ${TICALCS_LIBRARIES})
target_link_libraries(tikeys PRIVATE ${READLINE_LIBRARIES})

target_link_directories(tikeys PRIVATE ${GLIB_LIBRARY_DIRS})
target_link_directories(tikeys PRIVATE ${TICABLES_LIBRARIES})
target_link_directories(tikeys PRIVATE ${TICALCS_LIBRARIES})
target_link_directories(tikeys PRIVATE ${READLINE_LIBRARIES})