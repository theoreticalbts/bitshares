#include "ClientWrapper.hpp"

#include <bts/blockchain/time.hpp>
#include <bts/net/upnp.hpp>
#include <bts/net/config.hpp>
#include <bts/db/exception.hpp>

#include <QGuiApplication>
#include <QResource>
#include <QSettings>
#include <QJsonDocument>
#include <QUrl>
#include <QMessageBox>
#include <QDir>

#include <iostream>

ClientWrapper::ClientWrapper(QObject *parent)
   : QObject(parent),
     _main_thread(&fc::thread::current()),
     _bitshares_thread("bitshares"),
     _settings("BitShares", BTS_BLOCKCHAIN_NAME)
{
   _block_interval_timer.setInterval(1000);
   connect(&_block_interval_timer, &QTimer::timeout, this, &ClientWrapper::state_changed);
   _block_interval_timer.start();
}

ClientWrapper::~ClientWrapper()
{
   _init_complete.wait();
   QSettings("BitShares", BTS_BLOCKCHAIN_NAME).setValue("crash_state", "no_crash");
   if (_client)
      _bitshares_thread.async([this]{
         _client->stop();
         _client_done.wait();
         _client.reset();
      }).wait();
}

void ClientWrapper::handle_crash()
{
   auto response = QMessageBox::question(nullptr,
                                         tr("Crash Detected"),
                                         tr("It appears that %1 crashed last time it was running. "
                                            "If this is happening frequently, it could be caused by a "
                                            "corrupted database. Would you like "
                                            "to reset the database (this will take several minutes) or "
                                            "to continue normally? Resetting the database will "
                                            "NOT lose any of your information or funds.").arg(qApp->applicationName()),
                                         tr("Reset Database"),
                                         tr("Continue Normally"),
                                         QString(), 1);
   if (response == 0)
      QDir((get_data_dir() + "/chain")).removeRecursively();
}

QString ClientWrapper::get_data_dir()
{
   QString data_dir = QString::fromStdWString(bts::client::get_data_dir(boost::program_options::variables_map()).generic_wstring());
   if (_settings.contains("data_dir"))
      data_dir = _settings.value("data_dir").toString();
   int data_dir_index = qApp->arguments().indexOf("--data-dir");
   if (data_dir_index != -1 && qApp->arguments().size() > data_dir_index+1)
      data_dir = qApp->arguments()[data_dir_index+1];

   return data_dir;
}

void ClientWrapper::initialize()
{
   bool upnp = _settings.value( "network/p2p/use_upnp", true ).toBool();

   std::string default_wallet_name = _settings.value("client/default_wallet_name", "default").toString().toStdString();
   _settings.setValue("client/default_wallet_name", QString::fromStdString(default_wallet_name));

#ifdef _WIN32
   _cfg.rpc.rpc_user = "";
   _cfg.rpc.rpc_password = "";
#else
   _cfg.rpc.rpc_user     = "randomuser";
   _cfg.rpc.rpc_password = fc::variant(fc::ecc::private_key::generate()).as_string();
#endif
   _cfg.rpc.httpd_endpoint = fc::ip::endpoint::from_string( "127.0.0.1:9999" );
   _cfg.rpc.httpd_endpoint.set_port(0);
   ilog( "config: ${d}", ("d", fc::json::to_pretty_string(_cfg) ) );

   auto data_dir = get_data_dir();
   wlog("Starting client with data-dir: ${ddir}", ("ddir", fc::path(data_dir.toStdWString())));

   _init_complete = _bitshares_thread.async( [=](){
      try
      {
         _main_thread->async( [&]{ Q_EMIT status_update(tr("Starting %1").arg(qApp->applicationName())); });
         _client = std::make_shared<bts::client::client>("qt_wallet");
         _client->open( data_dir.toStdWString(), fc::optional<fc::path>(), [=](float progress) {
            _main_thread->async( [=]{ Q_EMIT status_update(tr("Reindexing database... Approximately %1% complete.").arg(progress, 0, 'f', 0)); } );
         } );

         if(!_client->get_wallet()->is_enabled())
            _main_thread->async([&]{ Q_EMIT error(tr("Wallet is disabled in your configuration file. Please enable the wallet and relaunch the application.")); });

         _main_thread->async( [&]{ Q_EMIT status_update(tr("Loading...")); });
         _client->configure( data_dir.toStdWString() );
         _client->init_cli();

         _main_thread->async( [&]{ Q_EMIT status_update(tr("Connecting to %1 network").arg(qApp->applicationName())); });
         _client->listen_on_port(0, false /*don't wait if not available*/);
         fc::ip::endpoint actual_p2p_endpoint = _client->get_p2p_listening_endpoint();

         _client->set_daemon_mode(true);
         _client_done = _client->start();
         if( !_actual_httpd_endpoint )
         {
            _main_thread->async( [&]{ Q_EMIT error( tr("Unable to start HTTP server...")); });
         }

         _client->start_networking([=]{
            for (std::string default_peer : _cfg.default_peers)
               _client->connect_to_peer(default_peer);
         });

         if( upnp )
         {
            _upnp_service = std::make_shared<bts::net::upnp_service>();
            _upnp_service->map_port( actual_p2p_endpoint.port() );
         }

         try
         {
            _client->wallet_open(default_wallet_name);
         }
         catch(...)
         {}
      }
      catch (const bts::db::db_in_use_exception&)
      {
         _main_thread->async( [&]{ Q_EMIT error( tr("An instance of %1 is already running! Please close it and try again.").arg(qApp->applicationName())); });
      }
      catch (...)
      {
         ilog("Failure when attempting to initialize client");
         if (fc::exists(fc::path(data_dir.toStdWString()) / "chain")) {
            fc::remove_all(fc::path(data_dir.toStdWString()) / "chain");
            _main_thread->async( [&]{ Q_EMIT error( tr("An error occurred while trying to start. Please try restarting the application.")); });
         } else
            _main_thread->async( [&]{ Q_EMIT error( tr("An error occurred while trying to start.")); });
      }
   });
   _init_complete.on_complete([this](fc::exception_ptr) {
      Q_EMIT initialization_complete();
   });
}


QString ClientWrapper::get_info()
{
   fc::variant_object result = _bitshares_thread.async( [this](){ return _client->get_info(); }).wait();
   return QString::fromStdString(fc::json::to_pretty_string( result ));
}


void ClientWrapper::set_data_dir(QString data_dir)
{
   QSettings ("BitShares", BTS_BLOCKCHAIN_NAME).setValue("data_dir", data_dir);
}

void ClientWrapper::confirm_and_set_approval(QString delegate_name, bool approve)
{
   auto account = get_client()->blockchain_get_account(delegate_name.toStdString());
   if( account.valid() && account->is_delegate() )
   {
      if( QMessageBox::question(nullptr,
                                tr("Set Delegate Approval"),
                                tr("Would you like to update approval rating of Delegate %1 to %2?")
                                .arg(delegate_name)
                                .arg(approve?"Approve":"Disapprove")
                                )
          == QMessageBox::Yes )
         get_client()->wallet_account_set_approval(delegate_name.toStdString(), approve);
   }
   else
      QMessageBox::warning(nullptr, tr("Invalid Account"), tr("Account %1 is not a delegate, so its approval cannot be set.").arg(delegate_name));
}