#include "globals.h"
#include "jde/fwk/process/process.h"
#include <jde/db/meta/AppSchema.h>
#include <jde/access/access.h>
#include <jde/app/client/IAppClient.h>
#include <jde/opc/server/UAServer.h>
#include "OpcServerAppClient.h"
#include "access/OpcAuthorize.h"
#include "pubsub/PubSubReader.h"

#define let const auto
namespace Jde::Opc {
	sp<DB::AppSchema> _appSchema;
	vector<UA_DataTypeArray> _customTypes;//before _ua:  destroyed after it.
	up<Server::UAServer> _ua;
	up<Server::PubSubReader> _pubSub;//after _ua: destroyed first, its connection belongs to that server.
	static sp<App::Client::IAppClient> _appClient = ms<Server::OpcServerAppClient>();

	α Server::Initialize( sp<DB::AppSchema> schema )ε->void{
		_appSchema = schema;
		_pubSub.reset();//a re-Initialize (test fixtures) replaces the server it was built on.
		_ua.reset();//before the next is made:  its historian takes the lock on hist.path that this one's holds.
		auto nodeAccess = dynamic_pointer_cast<OpcAuthorize>( schema->Authorizer );//installed by Startup.
		THROW_IF( !nodeAccess, "The opc schema's authorizer is not the OpcAuthorize Startup installs." );
		_ua = mu<UAServer>( ReadUASettings(), _appClient, move(nodeAccess) );//throws
		static std::once_flag registered;//once, not per Initialize:  it resets whichever server is current.
		std::call_once( registered, []{
			Process::AddShutdownFunction( [](bool, SL){
				_pubSub.reset();
				_ua.reset();
			} );
		} );
	}

	//The username token's login list:  /opc/users ([{name,password}]).  An opt-in - nothing shipped configures it
	//(reviews/install-issues.md #1:  a fresh install logs in with Google, by ruling no username is seeded); the test configs
	//do, for the gateway's password login (PasswordTests).  Plain text, on purpose:  a hashed per-user store is the hub's
	//job.  A malformed entry is logged and skipped.
	Ω readUsers()ι->vector<std::pair<string,string>>{
		vector<std::pair<string,string>> y;
		let users = Settings::FindArray( "/opc/users" );
		if( !users )
			return y;
		for( let& v : *users ){
			let name = v.is_object() ? Json::FindDefaultSV( v.get_object(), "name" ) : sv{};
			if( !name.empty() )
				y.emplace_back( string{name}, string{Json::FindDefaultSV(v.get_object(), "password")} );
			else
				WARNT( (ELogTags)EOpcLogTags::Server, "/opc/users: a user without a name is skipped." );
		}
		return y;
	}
	α Server::ReadUASettings()ε->UASettings{
		//A missing block is far likelier a hidden `ssl::` or a mistyped key than a decision, so it reads as the configuration
		//error it is, and the server does not start (UAConfigTests.NoSslIsRefused, security-matrix #5).
		let ssl = Settings::FindObject( "/opcServer/ssl" );
		THROW_IF( !ssl, "No '/opcServer/ssl':  the OPC UA server runs only with a certificate - its secured policies, beside the None endpoint clients discover it through.  Every shipped config sets it; look for a hidden `ssl::` or a mistyped key." );
		UASettings y{ .Ssl{*ssl} };
		let resource = ResourceSuffix();
		y.ApplicationName = resource.empty() ? string{"Jde-Cpp OpcServer"} : Ƒ( "Jde-Cpp OpcServer [{}]", resource );
		y.Port = Settings::FindNumber<PortType>( "/opcServer/port" ).value_or( 4840 );
		y.Address = Settings::FindString( "/opcServer/address" ).value_or( "" );//absent or null:  every interface.
		for( let& dir : Settings::FindStringArray("/access/trustedCertDirs") )
			y.TrustedCertDirs.emplace_back( dir );
		y.Tokens.Anonymous = Settings::FindBool( "/opc/tokenTypes/anonymous" ).value_or( false );
		y.Tokens.Certificate = Settings::FindBool( "/opc/tokenTypes/certificate" ).value_or( true );
		y.Tokens.Issued = Settings::FindBool( "/opc/tokenTypes/issued" ).value_or( true );
		if( Settings::FindBool("/opc/tokenTypes/username").value_or(true) )
			y.Users = readUsers();
		if( let uri = Settings::FindString("/opc/userTokenPolicyUri"); uri )
			y.UserTokenPolicyUri = *uri;
		if( let hist = Settings::FindObject("/opcServer/hist"); hist ){
			if( let timeout = Settings::FindDuration("/opcServer/hist/editTimeout"); timeout ){
				THROW_IF( *timeout<=Duration::zero(), "hist.editTimeout must be positive, not {} ms.", duration_cast<milliseconds>(*timeout).count() );
				y.EditTimeout = *timeout;
			}
			y.Hist.emplace( *hist, Process::AppDataFolder()/"logs"/"hist"/"opc-server" );
		}
		else
			INFOT( (ELogTags)EOpcLogTags::Opc, "No '/opcServer/hist':  this server keeps no history." );
		return y;
	}

	α Server::ResourceSuffix()ι->string{ return Settings::FindString( "/opcServer/resource" ).value_or( "" ); }
	α Server::InstanceSchema()ι->string{ return Access::InstanceSchemaName( "opc", ResourceSuffix() ); }
	α Server::AppClient()ι->sp<App::Client::IAppClient>{ return _appClient; }

	α Server::GetSchema()ι->DB::AppSchema&{ return *_appSchema; }
	α Server::GetSchemaPtr()ι->sp<DB::AppSchema>{ return _appSchema; }
	α Server::GetUAServer()ι->UAServer&{ return *_ua; }
	α Server::FindUAServer()ι->UAServer*{ return _ua.get(); }
	α Server::CustomTypes()ι->vector<UA_DataTypeArray>&{ return _customTypes; }
	α Server::StartPubSub( const jobject& settings, SL sl )ε->void{ _pubSub = mu<PubSubReader>( GetUAServer(), settings, sl ); }
	α Server::PubSub()ι->PubSubReader*{ return _pubSub.get(); }
}