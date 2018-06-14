#include <iostream>
#include <utility>
#include <map>
#include <algorithm>
#include <fc/log/logger.hpp>
#include "cybex/parse_url.hpp"

using namespace std;

namespace graphene { namespace cybex {


static map<string, ActionValue> action_map;


int parse_url(const string &url, parse_result &result)
{
    if (url.empty()) {
        return 0;
    }

    size_t sp = url.find_first_of('?');
    string params;
    string path;
    if (sp != string::npos){
        path=string(url.begin(), url.begin() + sp);
	params=string(url.begin() + sp + 1, url.end());
    }else{
	path = url;
    }

    int status;
    auto it = action_map.find(path);
    if(it == action_map.end())
    {
       status=0;
    } else {
        status=1;
        result.action=it->second;
    }
    if (status == 1 && !params.empty()){
        status = split_params(params, result.params);
    }

    return status;
}


//   get parameters  
int split_params(string &input, map<string, string> &result)
{
    int success = 1;
    while(!input.empty()){
        string current_param,rest;
        size_t sp = input.find_first_of('&');
	if (sp != string::npos){
	    current_param = string(input.begin(), input.begin() + sp);
	    rest = string(input.begin() + sp + 1, input.end());
	    input = rest;
	}else{
	    current_param=input;
	    input.clear();
	}

        sp = current_param.find_first_of('=');
        if (sp == string::npos){
	    success = 0;
            break;
	}
	string param_name(current_param.begin(), current_param.begin() + sp);
	string param_value(current_param.begin() + sp + 1, current_param.end());
	result.insert(pair<string,string>(param_name, param_value));
    } 

    return success;
}

void Initialize_url_parser()
{
    action_map["/market/ticker"] = ID_ticker;
    action_map["/market/volume_24"] = ID_volume;
    action_map["/market/trade/history"] = ID_trade;
    action_map["/market/history"] = ID_market;
    action_map["/market/order/book"] = ID_order;
    action_map["/assets/list"] = ID_assets;
    action_map["/account"] = ID_account;
}

}}
