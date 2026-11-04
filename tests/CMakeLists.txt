# Unit tests: the parser of `7z l -slt` and the checks of member names
add_executable(test_listing test_listing.cpp)
target_link_libraries(test_listing fuse7z_backend)
add_test(NAME listing COMMAND test_listing)

# Starting the 7-Zip tool, with a fake tool that prints its arguments
file(COPY fake-7z.sh DESTINATION "${CMAKE_CURRENT_BINARY_DIR}"
     FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
add_executable(test_sevenzip test_sevenzip.cpp)
target_link_libraries(test_sevenzip fuse7z_backend)
target_compile_definitions(test_sevenzip PRIVATE FAKE_7Z="${CMAKE_CURRENT_BINARY_DIR}/fake-7z.sh")
add_test(NAME sevenzip COMMAND test_sevenzip WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

# Listing and extraction with the real 7-Zip tool, skipped without it
add_executable(test_backend test_backend.cpp)
target_link_libraries(test_backend fuse7z_backend)
add_test(NAME backend COMMAND test_backend WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

set_tests_properties(backend PROPERTIES SKIP_RETURN_CODE 77)
