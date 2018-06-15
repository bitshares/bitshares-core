#include <iostream>
#include <fstream>
#include "cybex/wallet_lib.hpp"


main(int argc, char * argv[])
{
    if(cybex::wallet::init(argc,argv)==1)
   {
     std::ifstream ifs("wallet.cmd");

     if(ifs.good())
     {
       std::string line;

       while(std::getline(ifs, line))
       {

         std::cout << line << std::endl;
         
         std::string result;
         int err=cybex::wallet::exec(line, result);
         std::cout << result << std::endl;
        
       }
    }
    cybex::wallet::exit();
  }
}
