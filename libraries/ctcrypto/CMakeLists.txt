cmake_minimum_required(VERSION 3.16)
find_package(OpenSSL REQUIRED)

add_library(bts_ctcrypto
   src/curve.cpp
   src/bulletproofs_plus.cpp
   src/clsag.cpp
)
target_include_directories(bts_ctcrypto PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(bts_ctcrypto PUBLIC OpenSSL::Crypto)
target_compile_features(bts_ctcrypto PUBLIC cxx_std_14)
target_compile_options(bts_ctcrypto PRIVATE -Wno-deprecated-declarations)

add_executable(ctcrypto_tests src/test_ct.cpp)
target_link_libraries(ctcrypto_tests PRIVATE bts_ctcrypto)
add_test(NAME ctcrypto COMMAND ctcrypto_tests)

add_executable(ct_fuzz src/fuzz_ct.cpp)
target_link_libraries(ct_fuzz PRIVATE bts_ctcrypto)

add_executable(ct_exchange src/exchange_main.cpp)
target_link_libraries(ct_exchange PRIVATE bts_ctcrypto)
target_compile_options(ct_exchange PRIVATE -Wno-deprecated-declarations)
