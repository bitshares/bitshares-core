#pragma once
 
#include <string>
#include <map>
#include <iostream>

using namespace std;

namespace graphene { namespace cybex {

 

enum ActionValue {
	ID_ticker = 0, 
	ID_volume,
	ID_trade,
	ID_market,
	ID_order,
	ID_assets,
	ID_account
	};

typedef struct {
	ActionValue action;
	map<string, string> params;
} parse_result;

int parse_url(const string &url, parse_result &result);

int split_params(string &input, map<string, string> &result);

void Initialize_url_parser();

}}

