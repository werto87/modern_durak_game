macro(myproject_enable_clang_build_analyzer
        TARGET
        )
        IF (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            find_program(CLANG_BUILD_ANALYZER ClangBuildAnalyzer)
            IF (CLANG_BUILD_ANALYZER)
                target_compile_options(${TARGET} PUBLIC "-ftime-trace")

                add_custom_target(
                runClangBuildAnalyzer ALL
                COMMAND ClangBuildAnalyzer --start ${CMAKE_BINARY_DIR}
                COMMENT "Start clang build analyzer"
                )

                add_dependencies(${TARGET} runClangBuildAnalyzer)
            
                add_custom_target(
                stopClangBuildAnalyzer ALL
                COMMAND ClangBuildAnalyzer --stop ${CMAKE_BINARY_DIR} ClangBuildAnalyzerSession.txt
                COMMENT "Stop clang build analyzer"
                )
                add_dependencies(stopClangBuildAnalyzer ${TARGET})
                
                add_custom_target(
                analyzeCompileTimeWithClangBuildAnalyzer ALL
                COMMAND ClangBuildAnalyzer --analyze ClangBuildAnalyzerSession.txt
                COMMENT "Analyze compile time with clang build analyzer"
                )
                add_dependencies(analyzeCompileTimeWithClangBuildAnalyzer stopClangBuildAnalyzer)
            ELSE ()
                message(WARNING "ClangBuildAnalyzer requested but not found")    
            ENDIF ()
        ELSE ()
            message(WARNING "myproject_ENABLE_CLANG_BUILD_ANALYZER enabled but project compiled without clang compiler. ClangBuildAnalyzer only works with clang. Please check your compiler")    
        ENDIF()
endmacro()