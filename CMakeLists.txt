cmake_minimum_required(VERSION 3.24)

project(
  hisol
  VERSION 0.1.0
  DESCRIPTION "HTTPS IPMI serial-over-LAN stream bridge"
  LANGUAGES CXX
)

find_package(Boost REQUIRED CONFIG COMPONENTS json system)
find_package(CLI11 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(
  hisol
  src/cli.cpp
  src/commands.cpp
  src/console.cpp
  src/cookie_jar.cpp
  src/http_client.cpp
  src/main.cpp
  src/sol_bridge.cpp
  src/sol_session.cpp
  src/stdio.cpp
  src/text.cpp
  src/trace.cpp
  src/tls.cpp
  src/url.cpp
)

target_compile_features(hisol PRIVATE cxx_std_20)

target_compile_definitions(
  hisol
  PRIVATE
    BOOST_ASIO_NO_DEPRECATED
    BOOST_BEAST_USE_STD_STRING_VIEW
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    _WIN32_WINNT=0x0A00
)

if(MSVC)
  target_compile_options(hisol PRIVATE /W4 /permissive-)
else()
  target_compile_options(hisol PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(TARGET Boost::headers)
  set(HISOL_BOOST_HEADERS_TARGET Boost::headers)
elseif(TARGET Boost::boost)
  set(HISOL_BOOST_HEADERS_TARGET Boost::boost)
else()
  message(FATAL_ERROR "Boost headers target was not provided by find_package(Boost).")
endif()

target_link_libraries(
  hisol
  PRIVATE
    ${HISOL_BOOST_HEADERS_TARGET}
    Boost::json
    Boost::system
    CLI11::CLI11
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
)
