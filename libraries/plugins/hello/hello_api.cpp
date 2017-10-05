#include <graphene/app/application.hpp>
#include <graphene/chain/database.hpp>

#include <graphene/hello/hello_api.hpp>
#include <graphene/hello/hello_plugin.hpp>

namespace graphene { namespace hello {

/*
 * The detail is used to separated the implementation from the interface
 */
namespace detail {

class hello_api_impl
{
   public:
      hello_api_impl( graphene::app::application& _app );
      graphene::app::application& app;
      std::shared_ptr< graphene::hello_plugin::hello_plugin > get_plugin();

      // TODO:  Add API methods here
      uint32_t hello();
};

/**
 * Constructor Implementation
 */
hello_api_impl::hello_api_impl( graphene::app::application& _app ) : app( _app )
{}

/**
 * Get plugin name Implementation
 */
std::shared_ptr< graphene::hello_plugin::hello_plugin > hello_api_impl::get_plugin()
{
   /// We are loading the hello_plugin's get_plugin all here
   return app.get_plugin< graphene::hello_plugin::hello_plugin >( "hello" );
}

/**
 * Custom call 'hello' implementation
 */
uint32_t hello_api_impl::hello()
{
   // Obtain access to the internal database objects
   std::shared_ptr< graphene::chain::database > db = app.chain_database();
   // Return the head block number
   return db->head_block_num();
}

} /// detail

/*
 * Now we are done with implementation/detail, let's do the interface (linking
 * implementation)
 */

/*
 * Plugin constructor implementation
 */
hello_api::hello_api( graphene::app::application& app )
{
   my = std::make_shared< detail::hello_api_impl >(app);
}

/*
 * Custom call 'hello' interface
 */
uint32_t hello_api::hello()
{
   return my->hello();
}

} } // graphene::hello
