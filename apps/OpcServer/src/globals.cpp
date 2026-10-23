#include "globals.h"
#include "jde/fwk/process/process.h"
#include <jde/db/meta/AppSchema.h>
#include <jde/access/access.h>
#include <jde/app/client/IAppClient.h>
#include <jde/opc/server/UAServer.h>
#include "OpcServerAppClient.h"
#include "access/OpcAuthorize.h"
#include "hist/OpcServerHistoryHost.h"
#include "pubsub/PubSubReader.h"

#define let const auto
namespace Jde::Opc {
	sp<DB::AppSchema> _appSchema;
	sp<Server::OpcServerHistoryHost> _history;//holds the historian, so after _ua, which flushes nothing of its own.
	up<Server::UAServer> _ua;
	up<Server::PubSubReader> _pubSub;//after _ua: destroyed first, its connection belongs to that server.
	static sp<App::Client::IAppClient> _appClient = ms<Server::OpcServerAppClient>();

	α Server::Initialize( sp<DB::AppSchema> schema )ε->void{
		_appSchema = schema;
		_pubSub.reset();//a re-Initialize (test fixtures) replaces the server it was built on.
		_ua.reset();//both before the next is made:  its historian takes the lock on hist.path that this one's holds.
		_history.reset();
		auto nodeAccess = dynamic_pointer_cast<OpcAuthorize>( schema->Authorizer );//installed by Startup.
		THROW_IF( !nodeAccess, "The opc schema's authorizer is not the OpcAuthorize Startup installs." );
		let settings = ReadUASettings();//throws
		auto history = ms<OpcServerHistoryHost>( ReadHistSettings() );
		_ua = mu<UAServer>( settings, _appClient, move(nodeAccess), history );//throws, and the historian and its lock end with history.
		_history = move( history );
		static std::once_flag registered;//once, not per Initialize:  it resets whichever server is current.
		std::call_once( registered, []{
			Process::AddShutdownFunction( [](bool, SL){
				_pubSub.reset();
				_ua.reset();
				_history.reset();
			} );
		} );
	}

	α Server::ReadUASettings()ε->UASettings{
		//A missing block is far likelier a hidden `ssl::` or a mistyped key than a decision, so it reads as the configuration
		//error it is, and the server does not start (ReadUASettingsTests.NoSslIsRefused, security-matrix #5).  Here, not in
		//UAConfig, which sees a CryptoSettings either way:  an empty block is the default paths.
		let ssl = Settings::FindObject( "/opcServer/ssl" );
		THROW_IF( !ssl, "No '/opcServer/ssl':  the OPC UA server runs only with a certificate - its secured policies, beside the None endpoint clients discover it through.  Every shipped config sets it; look for a hidden `ssl::` or a mistyped key." );
		UASettings y{ .Ssl{*ssl} };
		let resource = ResourceSuffix();
		y.ApplicationName = resource.empty() ? string{"Jde-Cpp OpcServer"} : Ƒ( "Jde-Cpp OpcServer [{}]", resource );
		y.Port = Settings::FindNumber<PortType>( "/opcServer/port" ).value_or( y.Port );
		y.Address = Settings::FindString( "/opcServer/address" ).value_or( y.Address );//absent or null:  every interface.
		for( let& dir : Settings::FindStringArray("/access/trustedCertDirs") )
			y.TrustedCertDirs.emplace_back( dir );
		y.ReadTokens( "/opc" );//the test configs' /opc/users is the gateway's password login (PasswordTests).
		if( let timeout = Settings::FindDuration("/opcServer/hist/editTimeout"); timeout ){
			THROW_IF( *timeout<=Duration::zero(), "hist.editTimeout must be positive, not {} ms.", duration_cast<milliseconds>(*timeout).count() );
			y.EditTimeout = *timeout;
		}
		return y;
	}
	α Server::ReadHistSettings()ε->optional<Hist::Settings>{
		let hist = Settings::FindObject( "/opcServer/hist" );
		if( !hist ){
			INFOT( (ELogTags)EOpcLogTags::Opc, "No '/opcServer/hist':  this server keeps no history." );
			return nullopt;
		}
		return Hist::Settings{ *hist, Process::AppDataFolder()/"logs"/"hist"/"opc-server" };
	}

	α Server::ResourceSuffix()ι->string{ return Settings::FindString( "/opcServer/resource" ).value_or( "" ); }
	α Server::InstanceSchema()ι->string{ return Access::InstanceSchemaName( "opc", ResourceSuffix() ); }
	α Server::AppClient()ι->sp<App::Client::IAppClient>{ return _appClient; }

	α Server::GetSchema()ι->DB::AppSchema&{ return *_appSchema; }
	α Server::GetSchemaPtr()ι->sp<DB::AppSchema>{ return _appSchema; }
	α Server::GetUAServer()ι->UAServer&{ return *_ua; }
	α Server::FindUAServer()ι->UAServer*{ return _ua.get(); }
	α Server::HistoryHost()ι->OpcServerHistoryHost&{ return *_history; }
	α Server::StartPubSub( const jobject& settings, SL sl )ε->void{ _pubSub = mu<PubSubReader>( GetUAServer(), settings, sl ); }
	α Server::PubSub()ι->PubSubReader*{ return _pubSub.get(); }
}