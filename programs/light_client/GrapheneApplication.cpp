#include "GrapheneApplication.hpp"
#include "ChainDataModel.hpp"
#include "Operations.hpp"

#include <graphene/app/api.hpp>

#include <fc/rpc/websocket_api.hpp>

using graphene::app::login_api;
using graphene::app::database_api;

GrapheneApplication::GrapheneApplication(QObject* parent)
:QObject(parent),m_thread("app")
{
   connect(this, &GrapheneApplication::queueExecute,
           this, &GrapheneApplication::execute);

   m_model = new ChainDataModel(m_thread, this);
   m_operationBuilder = new OperationBuilder(*m_model, this);

   connect(m_model, &ChainDataModel::queueExecute,
           this, &GrapheneApplication::execute);

   connect(m_model, &ChainDataModel::exceptionThrown,
           this, &GrapheneApplication::exceptionThrown);
}

GrapheneApplication::~GrapheneApplication()
{
}

void GrapheneApplication::setIsConnected(bool v)
{
   if (v != m_isConnected)
   {
      m_isConnected = v;
      Q_EMIT isConnectedChanged(m_isConnected);
   }
}

void GrapheneApplication::start(QString apiurl, QString user, QString pass)
{
   if (!m_thread.is_current())
   {
      m_done = m_thread.async([=](){ return start(apiurl, user, pass); });
      return;
   }
   try {
      m_client = std::make_shared<fc::http::websocket_client>();
      ilog("connecting...${s}", ("s",apiurl.toStdString()));
      auto con = m_client->connect(apiurl.toStdString());
      m_connectionClosed = con->closed.connect([this]{queueExecute([this]{setIsConnected(false);});});
      auto apic = std::make_shared<fc::rpc::websocket_api_connection>(*con);
      auto remote_api = apic->get_remote_api<login_api>(1);
      auto db_api = apic->get_remote_api<database_api>(0);
      if (!remote_api->login(user.toStdString(), pass.toStdString()))
      {
         elog("login failed");
         Q_EMIT loginFailed();
         return;
      }

      ilog("connecting...");
      queueExecute([=](){
         m_model->setDatabaseAPI(db_api);
      });

      queueExecute([=](){ setIsConnected(true); });
   } catch (const fc::exception& e)
   {
      Q_EMIT exceptionThrown(QString::fromStdString(e.to_string()));
   }
}

Q_SLOT void GrapheneApplication::execute(const std::function<void()>& func)const
{
   func();
}


