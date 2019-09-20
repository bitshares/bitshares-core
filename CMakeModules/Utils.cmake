macro(add_flag_append _VAR_NAME _FLAG)
	set(${_VAR_NAME} "${${_VAR_NAME}} ${_FLAG}")
endmacro(add_flag_append _VAR_NAME _FLAG)

macro(add_linker_flag _FLAG)
	#executables
	add_flag_append(CMAKE_C_LINK_FLAGS "-Wl,${_FLAG}")
	add_flag_append(CMAKE_CXX_LINK_FLAGS "-Wl,${_FLAG}")
	#libraries
	add_flag_append(CMAKE_SHARED_LIBRARY_C_FLAGS "-Wl,${_FLAG}")
	add_flag_append(CMAKE_SHARED_LIBRARY_CXX_FLAGS "-Wl,${_FLAG}")
endmacro(add_linker_flag _FLAG)

include(CheckCXXCompilerFlag)

macro(add_compiler_flag_if_available _FLAG)
	string(TOUPPER "${_FLAG}" _TEMPVAR1)
	string(REPLACE "-" "_" _TEMPVAR2 "${_TEMPVAR1}")
	string(CONCAT _TEMPVAR3 "HAVE" "${_TEMPVAR2}")
	set(CMAKE_REQUIRED_FLAGS "${_FLAG}")
	check_cxx_compiler_flag("${_FLAG}" ${_TEMPVAR3})
	if(${_TEMPVAR3})
		add_flag_append(CMAKE_CXX_FLAGS ${_FLAG})
	endif()
endmacro(add_compiler_flag_if_available _VAR_NAME _FLAG)
