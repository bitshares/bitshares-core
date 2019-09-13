#include "../common/application_helper.hpp"

int main(int argc, char** argv)
{
   graphene::test::application_runner app;
   app.start();
   std::string p2p_address = "127.0.0.1:" + std::to_string( app.p2p_port_number );
   std::cout << "Running on " << p2p_address << std::endl;

   // add node
   if (argc > 1)
   {
      app.add_seed_node(argv[1]);
   }

   std::cout << "Press e to exit" << std::endl;
   char c = 0;
   while( c != 'e' )
      std::cin >> c;
}