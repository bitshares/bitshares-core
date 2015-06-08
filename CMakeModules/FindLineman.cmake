find_program(NPM_EXECUTABLE npm)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args("NPM" DEFAULT_MSG NPM_EXECUTABLE)

find_program(LINEMAN_EXECUTABLE lineman)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args("Lineman" DEFAULT_MSG LINEMAN_EXECUTABLE)

