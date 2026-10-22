cmake_minimum_required(VERSION 3.14)
project(ContractTerm LANGUAGES CXX)

find_package(Boost REQUIRED)

add_library(contract_term ContractTerm.cpp)
target_include_directories(contract_term
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${Boost_INCLUDE_DIRS}
)
target_compile_features(contract_term PUBLIC cxx_std_14)

add_executable(contract_term_tests ContractTermTests.cpp)
target_link_libraries(contract_term_tests PRIVATE contract_term)
target_compile_features(contract_term_tests PRIVATE cxx_std_14)

enable_testing()
add_test(NAME contract_term_tests COMMAND contract_term_tests)
