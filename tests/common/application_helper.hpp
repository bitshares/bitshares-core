#pragma once
#include <memory>
#include <graphene/app/application.hpp>

namespace graphene { namespace test {

/**
 * Handles creating a running node
 */
class application_runner
{
   public:
   application_runner();
   int server_port_number;
   const graphene::app::application& get_app();
   void add_seed_node(int remote_port);
   private:
   std::shared_ptr<graphene::app::application> _app;
   fc::temp_directory _dir;
   static int get_available_port();
};

}}
