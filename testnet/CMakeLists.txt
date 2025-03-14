cmake_minimum_required(VERSION 3.10)
project(BinTestnet)

set(CMAKE_CXX_STANDARD 17)

set(Boost_USE_STATIC_LIBS ON)  # Use static libraries

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

add_definitions(
    -UNDEBUG
    -DDTF_HEADER_ONLY
)

include_directories(
    ../include
)

if (DEFINED ${BOOST_INCLUDE_DIR})
    include_directories(
        ${BOOST_INCLUDE_DIR}
    )
endif()

set(BINAPI_HEADERS
    binapi/api.hpp
    binapi/flatjson.hpp
    binapi/dtf.hpp
    binapi/double_type.hpp
    binapi/enums.hpp
    binapi/errors.hpp
    binapi/invoker.hpp
    binapi/message.hpp
    binapi/pairslist.hpp
    binapi/reports.hpp
    binapi/tools.hpp
    binapi/types.hpp
    binapi/websocket.hpp
)

set(BINAPI_SOURCES
    ../src/api.cpp
    ../src/enums.cpp
    ../src/errors.cpp
    ../src/pairslist.cpp
    ../src/reports.cpp
    ../src/tools.cpp
    ../src/types.cpp
    ../src/websocket.cpp
)

add_executable(
    ${PROJECT_NAME}
    #
    main.cpp
    #
    ${BINAPI_SOURCES}
)

if (DEFINED ${BOOST_LIB_DIR})
    target_link_libraries(
        ${PROJECT_NAME}
        -L${BOOST_LIB_DIR}
    )
endif()

target_link_libraries(
    ${PROJECT_NAME}
    z
    crypto
    ssl
    pthread
)

# Set CPack generator to DEB (Debian package)
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_CONTACT "dswawbdo@gmail.com")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_PACKAGE_VERSION "0.1.1")
# Include additional libraries and headers
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

# Include the CPack module to enable packaging
include(CPack)
