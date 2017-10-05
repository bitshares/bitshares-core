#include <graphene/hello/hello_api.hpp>
#include <graphene/hello/hello_plugin.hpp>

#include <string>

namespace graphene { namespace hello_plugin {

/*
 * The detail is used to separated the implementation from the interface
 */
namespace detail {

class hello_plugin_impl
{
   public:
      hello_plugin_impl();
      virtual ~hello_plugin_impl();

      virtual std::string plugin_name()const;
      virtual void plugin_initialize( const boost::program_options::variables_map& options );
      virtual void plugin_startup();
      virtual void plugin_shutdown();

      // TODO:  Add custom methods here
};

/**
 * Constructor Implementation
 */
hello_plugin_impl::hello_plugin_impl()
{
}

/**
 * Destructor Implementation
 */
hello_plugin_impl::~hello_plugin_impl() {}

/**
 * Get plugin name Implementation
 */
std::string hello_plugin_impl::plugin_name()const
{
   return "hello_api";
}

/*
 * Initialize Implementation
 */
void hello_plugin_impl::plugin_initialize( const boost::program_options::variables_map& options )
{
   ilog("hello plugin:  plugin_initialize()");
}

/*
 * Plugin Startup implementation
 */
void hello_plugin_impl::plugin_startup()
{
   ilog("hello plugin:  plugin_startup()");
}

/*
 * Plugin Shutdown implementation
 */
void hello_plugin_impl::plugin_shutdown()
{
   ilog("hello plugin:  plugin_shutdown()");
}

} /// detail

/*
 * Now we are done with implementation/detail, let's do the interface (linking
 * implementation)
 */


hello_plugin::hello_plugin() :
   my( new detail::hello_plugin_impl )
{
}

hello_plugin::~hello_plugin() {}

std::string hello_plugin::plugin_name()const
{
   return my->plugin_name();
}

void hello_plugin::plugin_initialize( const boost::program_options::variables_map& options )
{
   my->plugin_initialize( options );
}

void hello_plugin::plugin_startup()
{
   my->plugin_startup();
}

void hello_plugin::plugin_shutdown()
{
   my->plugin_shutdown();
}

} } // graphene::hello_plugin
