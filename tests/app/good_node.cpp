#include "../common/application_helper.hpp"
#include <fc/log/logger.hpp>
#include <fc/log/logger_config.hpp>
#include <thread>
#include <memory>

namespace fc {
   extern std::unordered_map<std::string,logger>& get_logger_map();
   extern std::unordered_map<std::string,appender::ptr>& get_appender_map();
}

int main(int argc, char** argv)
{
   fc::temp_directory td;
   std::shared_ptr<fc::path> temp_dir = nullptr;
   std::string remote_node_ip;
   for (int i = 1; i < argc; i++)
   {
      if (std::string(argv[i]) == "-d")
      {
         temp_dir = std::make_shared<fc::path>( fc::path(argv[i+1]));
      }
      if (std::string(argv[i]) == "-s")
         remote_node_ip = argv[i+1];
   }
   // we were not passed a temp directory, create one
   if (!temp_dir)
      temp_dir = std::make_shared<fc::path>( td.path() );
   // start node
   graphene::test::application_runner app(temp_dir);
   app.start();
   std::string p2p_address = "127.0.0.1:" + std::to_string( app.p2p_port_number );

   // adjust logging
   auto log_map = fc::get_logger_map();
   auto appenders = fc::get_appender_map();
   auto p2p_logger = log_map["p2p"];
   p2p_logger.add_appender( appenders["stdout"] );
   p2p_logger.set_log_level(fc::log_level::debug);
   auto default_logger = log_map["default"];
   default_logger.set_log_level(fc::log_level::debug);
   std::cout << "Running on " << p2p_address << std::endl;

   // add node if passed in
   if (!remote_node_ip.empty())
   {
      std::this_thread::sleep_for(std::chrono::seconds(5));
      std::cout << "attempting to add node " << remote_node_ip << "\n";
      app.add_node(remote_node_ip);
   }

   std::cout << "Press e [enter] to exit" << std::endl;
   char c = 0;
   while( c != 'e' )
      std::cin >> c;
}