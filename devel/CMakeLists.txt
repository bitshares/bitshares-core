
file(GLOB CYBEX_HEADERS "include/wallet_lib.hpp")

add_library( cybex_wallet 
             STATIC 
             api_wrapper.cpp
             wallet_lib.cpp
           )


target_include_directories( cybex_wallet 
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" 
                                     "${CMAKE_SOURCE_DIR}/libraries/fc/include"
                                     "${CMAKE_SOURCE_DIR}/libraries/app/include"
                                     "${CMAKE_SOURCE_DIR}/libraries/chain/include"
                                     "${CMAKE_SOURCE_DIR}/libraries/db/include"
                                     "${CMAKE_SOURCE_DIR}/libraries/net/include"
                                     "${CMAKE_SOURCE_DIR}/libraries/egenesis/include"
                                     "${CMAKE_SOURCE_DIR}/libraries/wallet/include"
                                     "${CMAKE_SOURCE_DIR}/libraries/utilities/include"
                                     "${CMAKE_SOURCE_DIR}/libraries/fc/vendor/websocketpp"
                                     "${CMAKE_SOURCE_DIR}/libraries/plugins/market_history/include"
                                     "${CMAKE_SOURCE_DIR}/libraries/plugins/grouped_orders/include"
                                     "${CMAKE_SOURCE_DIR}/libraries/plugins/debug_witness/include"
                          )

if( APPLE )
	find_package( Boost REQUIRED COMPONENTS program_options )
	include_directories( ${Boost_INCLUDE_DIRS} )
	find_package( openssl REQUIRED )
	include_directories( ${OPENSSL_INCLUDE_DIR} )
endif()


INSTALL( 
   TARGETS cybex_wallet
   ARCHIVE DESTINATION lib
   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
)


INSTALL( FILES ${CYBEX_HEADERS} DESTINATION "include/cybex" )
                      
