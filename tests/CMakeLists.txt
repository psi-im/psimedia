add_executable(payloadinfo_test payloadinfo_test.cpp)

target_link_libraries(payloadinfo_test PRIVATE gstprovidersrc)

add_test(NAME payloadinfo_test COMMAND payloadinfo_test)
