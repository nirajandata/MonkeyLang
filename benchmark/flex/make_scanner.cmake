if(NOT DEFINED SPEC OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "SPEC and OUTPUT are required")
endif()

file(READ "${SPEC}" spec_text)

if(USE_KEYWORD_RULES)
    file(READ "${RULES}" keyword_rules)
else()
    set(keyword_rules "")
endif()

string(REPLACE "@MONKEY_FLEX_KEYWORD_RULES@" "${keyword_rules}" spec_text
       "${spec_text}")

string(FIND "${spec_text}" "@MONKEY_FLEX_KEYWORD_RULES@" leftover)
if(NOT leftover EQUAL -1)
    message(FATAL_ERROR "unsubstituted marker left in ${OUTPUT}")
endif()

file(WRITE "${OUTPUT}" "${spec_text}")