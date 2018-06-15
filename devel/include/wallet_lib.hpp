#pragma once

#include <string>

namespace cybex { namespace wallet {


int init(int argc, char * argv[]);

int exec( const std::string & line, std::string & result);

int exit();



}}
