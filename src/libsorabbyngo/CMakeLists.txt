cmake_minimum_required(VERSION 3.16)
project(sorabbyngo VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Compiler flags ──────────────────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wformat=2 -Wstrict-prototypes
    -fstack-protector-strong
    -D_FORTIFY_SOURCE=2
)

# ── Find libsodium ──────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)
pkg_check_modules(NATS   REQUIRED libnats)
pkg_check_modules(CURL   REQUIRED libcurl)

# ── libsorabbyngo shared + static ───────────────────────────────────────────
set(LIB_SOURCES
    src/identity.c
    src/msg.c
    src/ai.c
)

set(LIB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Shared library (.so)
add_library(sorabbyngo_shared SHARED ${LIB_SOURCES})
target_include_directories(sorabbyngo_shared PUBLIC ${LIB_INCLUDE_DIR} ${SODIUM_INCLUDE_DIRS} ${NATS_INCLUDE_DIRS})
target_link_libraries(sorabbyngo_shared ${SODIUM_LIBRARIES} ${NATS_LIBRARIES} ${CURL_LIBRARIES} pthread)
set_target_properties(sorabbyngo_shared PROPERTIES
    OUTPUT_NAME "sorabbyngo"
    VERSION     ${PROJECT_VERSION}
    SOVERSION   0
)

# Static library (.a)
add_library(sorabbyngo_static STATIC ${LIB_SOURCES})
target_include_directories(sorabbyngo_static PUBLIC ${LIB_INCLUDE_DIR} ${SODIUM_INCLUDE_DIRS} ${NATS_INCLUDE_DIRS})
target_link_libraries(sorabbyngo_static ${SODIUM_LIBRARIES} ${NATS_LIBRARIES} ${CURL_LIBRARIES} pthread)
set_target_properties(sorabbyngo_static PROPERTIES
    OUTPUT_NAME "sorabbyngo"
)

# ── Tests ───────────────────────────────────────────────────────────────────
enable_testing()

add_executable(test_identity tests/test_identity.c)
target_include_directories(test_identity PRIVATE ${LIB_INCLUDE_DIR} ${SODIUM_INCLUDE_DIRS})
target_link_libraries(test_identity sorabbyngo_static ${SODIUM_LIBRARIES} ${NATS_LIBRARIES} ${CURL_LIBRARIES} pthread)

add_test(NAME identity COMMAND test_identity)

add_executable(test_msg tests/test_msg.c)
target_include_directories(test_msg PRIVATE ${LIB_INCLUDE_DIR} ${SODIUM_INCLUDE_DIRS} ${NATS_INCLUDE_DIRS})
target_link_libraries(test_msg sorabbyngo_static ${SODIUM_LIBRARIES} ${NATS_LIBRARIES} ${CURL_LIBRARIES} pthread)
add_test(NAME msg COMMAND test_msg)

# ── Install ─────────────────────────────────────────────────────────────────
install(TARGETS sorabbyngo_shared sorabbyngo_static
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/libsorabbyngo DESTINATION include)

# ── Summary ─────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "  ╔═══════════════════════════════════════╗")
message(STATUS "  ║  SORABBYNGO OS — libsorabbyngo v${PROJECT_VERSION}  ║")
message(STATUS "  ║  libsodium: ${SODIUM_VERSION} · libnats         ║")
message(STATUS "  ╚═══════════════════════════════════════╝")
message(STATUS "")

add_executable(test_ai tests/test_ai.c)
target_include_directories(test_ai PRIVATE ${LIB_INCLUDE_DIR} ${SODIUM_INCLUDE_DIRS} ${NATS_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS})
target_link_libraries(test_ai sorabbyngo_static ${SODIUM_LIBRARIES} ${NATS_LIBRARIES} ${CURL_LIBRARIES} pthread)
add_test(NAME ai COMMAND test_ai)
