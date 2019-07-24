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

