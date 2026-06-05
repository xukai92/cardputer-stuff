file(GLOB_RECURSE MY_SRCS
    "./hal/*.c"
    "./hal/*.cc"
    "./hal/*.cpp"
    "./apps/*.c"
    "./apps/*.cc"
    "./apps/*.cpp"
)

set(MY_INCLUDE_DIRS 
    "."
    "./assets"
    "./hal"
    "./apps"
)

set(PIKA_PYTHON_INCS
    ./apps/app_repl/PikaScript/pikascript-api/
    ./apps/app_repl/PikaScript/pikascript-core/
)

file(GLOB EXTERN_FILES assets/*)

idf_component_register(SRCS "main.cpp" ${MY_SRCS}
                    INCLUDE_DIRS ${MY_INCLUDE_DIRS} ${PIKA_PYTHON_INCS}
                    EMBED_FILES ${EXTERN_FILES}
)

# Build-time config for app_claude — keeps the bridge URL + token out of the
# (public) source tree. Pass them via an environment variable at build time:
#   CARDPUTER_CLAUDE_WS_URI='wss://host/?token=SECRET' idf.py build
# Without it, the placeholder in app_claude.h is used and the app won't connect.
if(DEFINED ENV{CARDPUTER_CLAUDE_WS_URI})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        "CARDPUTER_CLAUDE_WS_URI=\"$ENV{CARDPUTER_CLAUDE_WS_URI}\"")
endif()
