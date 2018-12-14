#include <graphene/app/plugin_type.hpp>
#include <typeinfo>
#include <map>
namespace graphene { namespace app{
   using std::map;
   using std::string;
   std::string get_plugin_name(const abstract_plugin &plg)
   {
      const std::type_info& info = typeid(plg);
/*
#ifdef __unix__
      string all_name = info.name();
      auto iter = find_if(all_name.rbegin(),all_name.rend(),[&](const char &c)->bool{
         if(isdigit(static_cast<const unsigned char>(c))) return true;
         return false;
      });
      size_t dis = std::distance(all_name.begin(),iter.base());
      if(dis != all_name.size() && all_name[all_name.size()-1] == 'E')
         return all_name.substr(dis,all_name.size()-dis-1);
      else if (dis != all_name.size())
         return all_name.substr(dis,all_name.size()-dis);
      return all_name;
#else
*/
      static map<size_t,string> plugin_record_name;
      if(!plugin_record_name.empty())
      {
PLUGIN:
         size_t hash = info.hash_code();
         if(plugin_record_name.find(hash) != plugin_record_name.end())
            return plugin_record_name[hash];
         else
            return "Unknown_plugin";
      }
      else
      {
         plugin_record_name[typeid(abstract_plugin).hash_code()] = "abstract_plugin";
         plugin_record_name[typeid(plugin).hash_code()] = "plugin";
         plugin_record_name[typeid(plugin_witness).hash_code()] = "witness_plugin";
         plugin_record_name[typeid(plugin_debug_witness).hash_code()] = "debug_witness_plugin";
         plugin_record_name[typeid(plugin_account_history).hash_code()] = "account_history_plugin";
         plugin_record_name[typeid(plugin_elasticsearch).hash_code()] = "elasticsearch_plugin";
         plugin_record_name[typeid(plugin_market_history).hash_code()] = "market_history_plugin";
         plugin_record_name[typeid(plugin_delayed_node).hash_code()] = "delayed_node_plugin";
         plugin_record_name[typeid(plugin_snapshot).hash_code()] = "snapshot_plugin";
         plugin_record_name[typeid(plugin_es_objects).hash_code()] = "es_objects_plugin";
         plugin_record_name[typeid(plugin_grouped_orders).hash_code()] = "grouped_orders_plugin";
         plugin_record_name[typeid(plugin_template).hash_code()] = "template_plugin";
         goto PLUGIN;
      }
//#endif
   }
}}
