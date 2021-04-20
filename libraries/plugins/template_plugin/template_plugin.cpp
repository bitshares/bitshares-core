/*
 * Copyright (c) 2018 template_plugin and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <graphene/template_plugin/template_plugin.hpp>

namespace graphene { namespace template_plugin {

namespace detail
{

class template_plugin_impl
{
   public:
      explicit template_plugin_impl( template_plugin& _plugin );
      virtual ~template_plugin_impl();

      void on_block( const signed_block& b );

      graphene::chain::database& database()
      {
         return _self.database();
      }

      friend class graphene::template_plugin::template_plugin;

   private:
      template_plugin& _self;

      std::string _plugin_option = "";

};

void template_plugin_impl::on_block( const signed_block& b )
{
   wdump((b.block_num()));
}

template_plugin_impl::template_plugin_impl( template_plugin& _plugin ) :
   _self( _plugin )
{
   // Put other code here
}

template_plugin_impl::~template_plugin_impl()
{
   // Put the real code here. If none, remove the destructor.
}

} // end namespace detail

template_plugin::template_plugin(graphene::app::application& app) :
   plugin(app),
   my( std::make_unique<detail::template_plugin_impl>(*this) )
{
   // Add needed code here
}

template_plugin::~template_plugin()
{
   cleanup();
}

std::string template_plugin::plugin_name()const
{
   return "template_plugin";
}
std::string template_plugin::plugin_description()const
{
   return "template_plugin description";
}

void template_plugin::plugin_set_program_options(
   boost::program_options::options_description& cli,
   boost::program_options::options_description& cfg
   )
{
   cli.add_options()
         ("template_plugin_option", boost::program_options::value<std::string>(), "template_plugin option")
         ;
   cfg.add(cli);
}

void template_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   database().applied_block.connect( [&]( const signed_block& b) {
      my->on_block(b);
   } );

   if (options.count("template_plugin") > 0) {
      my->_plugin_option = options["template_plugin"].as<std::string>();
   }
}

void template_plugin::plugin_startup()
{
   ilog("template_plugin: plugin_startup() begin");
}

void template_plugin::plugin_shutdown()
{
   ilog("template_plugin: plugin_shutdown() begin");
   cleanup();
}

void template_plugin::cleanup()
{
   // Add cleanup code here
}

} }
