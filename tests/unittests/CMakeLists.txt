add_executable(unittests
    DiagnosticTests.cpp
    EvalTests.cpp
    ExpressionParsingTests.cpp
    ExpressionTests.cpp
    FileTests.cpp
    HierarchyTests.cpp
    LexerTests.cpp
    main.cpp
    MemberParsingTests.cpp
    MemberTests.cpp
    NumericTests.cpp
    PortTests.cpp
    PreprocessorTests.cpp
    RewriterTests.cpp
    StatementParsingTests.cpp
    StatementTests.cpp
    SymbolLookupTests.cpp
    SystemFuncTests.cpp
    TypeTests.cpp
    UtilTests.cpp
)

target_link_libraries(unittests PRIVATE slang CONAN_PKG::Catch2)
target_link_libraries(unittests PRIVATE CONAN_PKG::fmt)

if(CI_BUILD)
    message("Running CI build")
    target_compile_definitions(unittests PRIVATE CI_BUILD)
endif()

# Copy the data directory for running tests from the build folder.
add_custom_command(
    TARGET unittests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data ${CMAKE_CURRENT_BINARY_DIR}/data
)

add_test(NAME unittests COMMAND unittests)
