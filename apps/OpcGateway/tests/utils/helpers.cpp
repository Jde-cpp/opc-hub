#include "helpers.h"
#include <jde/fwk/settings.h>
#include <jde/opc/uatypes/opcHelpers.h>
#include <jde/opc/UAException.h>
#include <jde/web/client/http/ClientHttpAwait.h>
#include <jde/app/client/IAppClient.h>
#include "../../src/GatewayAppClient.h"
#include "../../src/UAClient.h"
#include "../../src/auth/OpcServerSession.h"
#include "../../src/auth/UM.h"
#include "../../src/ql/GatewayQL.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	α CreateServerCnnctnAwait::Execute()ι->QL::QLAwait<jobject>::Task{
		try{
			str certificateUri = _certificateUri ? *_certificateUri : string{ Settings::FindSV("/opc/urn").value_or("urn:open62541.server.application") };
			str url = _url ? *_url : string{ Settings::FindSV("/opc/url").value_or("opc.tcp://127.0.0.1:4840") };
			let create = Ƒ( "mutation createServerConnection( slug:'{}', name:'{}', certificateUri:'{}', description:'Test basic functionality', url:'{}', isDefault:false ){{id}}",
				_slug.value_or( OpcServerSlug ),
				_slug ? *_slug : "My Test Server",//name is unique too - a second row of the caller's shape cannot share the default's.
				certificateUri,
				url
			);
			let createJson = co_await *QL().QueryObject( Str::Replace(create, '\'', '"'), {}, {UserPK::System}, true, _sl );
			ResumeScaler( Json::AsNumber<ServerCnnctnPK>(createJson, "id") );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α PurgeServerCnnctnAwait::Execute()ι->QL::QLAwait<>::Task{
		if( !_pk.has_value() )
			_pk = SelectServerCnnctn( OpcServerSlug )->Id;
		let q = Ƒ( "{{ mutation purgeServerConnection('id':{}) }}", *_pk );
		let result = co_await *QL().Query( Str::Replace(q, '\'', '"'), {}, {UserPK::System}, true, _sl );
		ResumeScaler( 1 );
	}
}

namespace Jde::Opc::Gateway{
	α Tests::CreateServerCnnctn()ε->ServerCnnctnPK{
		return BlockTAwait<ServerCnnctnPK>( CreateServerCnnctnAwait{} );
	}

	α Tests::PurgeServerCnnctn( optional<ServerCnnctnPK> pk )ι->uint{
		return BlockAwait<PurgeServerCnnctnAwait,uint>( PurgeServerCnnctnAwait{pk} );
	}

	α Tests::SelectServerCnnctn( DB::Key id )ι->optional<ServerCnnctn>{
		let select = Ƒ( "serverConnection({}){{ id name attributes created updated deleted slug description certificateUri isDefault url }}", id.QLInput() );
		auto o = QL().QuerySync<jobject>( select, id.QLVariables(), {UserPK::System} );
		return o.empty() ? optional<ServerCnnctn>{} : ServerCnnctn( move(o) );
	}

	α Tests::GetConnection( str slug )ε->ServerCnnctn{
		auto con = SelectServerCnnctn( {slug} );
		if( !con ){
			BlockTAwait<Access::ProviderPK>( ProviderMAwait{slug, false} );
			let id = BlockTAwait<ServerCnnctnPK>( CreateServerCnnctnAwait{} );
			con = SelectServerCnnctn( id );
		}
		return *con;
	}
	α Tests::GetConnection( str slug, str url, str certificateUri )ε->ServerCnnctn{
		auto con = SelectServerCnnctn( {slug} );
		if( !con ){
			BlockTAwait<Access::ProviderPK>( ProviderMAwait{slug, false} );//a stale provider row goes first, as above - the create's hook inserts the slug's own.
			let id = BlockTAwait<ServerCnnctnPK>( CreateServerCnnctnAwait{slug, url, certificateUri} );
			con = SelectServerCnnctn( id );
		}
		return *con;
	}

	using Web::Client::ClientHttpAwait;
	using Web::Client::ClientHttpRes;
	α Tests::Query( sv ql, jobject vars, bool raw )ε->jobject{
		try{
			jobject body{ {"query", ql} };
			if( !vars.empty() )
				body["variables"] = vars;
			auto res = BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{
				"localhost",
				Ƒ("/graphql?{}", raw ? "raw" : "" ),
				serialize(body),
				GatewayPort(),
				{ .Authorization=Ƒ("{:x}", AppClient()->SessionId()), .IsSsl=false }
			});
			return res.Json();
		}
		catch( runtime_error& e ){
		}
		return {};
	}

	//the policy and mode the session was opened with, read on the client's strand - the only place UA_Client_* calls may run.
	α Tests::Negotiated( const sp<UAClient>& client )ι->string{
		if( !client )
			return "ok";
		string y{ "ok" }; atomic_flag done;
		client->PostUA( [&]{
			UA_Variant policy{}, mode{};
			if( !UA_Client_getConnectionAttributeCopy(*client, UA_QUALIFIEDNAME(0, (char*)"securityPolicyUri"), &policy) && !UA_Client_getConnectionAttributeCopy(*client, UA_QUALIFIEDNAME(0, (char*)"securityMode"), &mode) ){
				constexpr array<sv,4> modes{ "Invalid", "None", "Sign", "SignAndEncrypt" };
				let uri = ToString( *(UA_String*)policy.data );
				y = Ƒ( "ok ({}/{})", uri.substr(uri.rfind('#')+1), FromEnum(modes, *(UA_MessageSecurityMode*)mode.data) );
			}
			UA_Variant_clear( &policy ); UA_Variant_clear( &mode );
			done.test_and_set(); done.notify_all();
		});
		done.wait( false );
		return y;
	}

	flat_map<string,ETokenType> _userTokens;
	α Tests::AvailableUserTokens( sv url_ )ε->ETokenType{
		str url{url_};
		if( _userTokens.contains(url) )
			return _userTokens[url];
		Logger logger;
		UA_ClientConfig config{};
		config.logging = &logger;
		config.tcpReuseAddr = true;
		UA_ClientConfig_setDefault( &config );
		auto UA_DateTime_now_fake = []( UA_EventLoop* ) -> UA_DateTime{ return 0x5C8F735D; };
		config.eventLoop->dateTime_now = UA_DateTime_now_fake;
		config.eventLoop->dateTime_nowMonotonic = UA_DateTime_now_fake;
		UA_Client *client = UA_Client_newWithConfig( &config );
    UA_EndpointDescription* endpointArray{}; uint endpointArraySize{};
		auto tokens = _userTokens.try_emplace( url ).first;
    UAε( UA_Client_getEndpoints(client, url.c_str(), &endpointArraySize, &endpointArray) ) ;
		for( auto ep : Iterable<UA_EndpointDescription>(endpointArray, endpointArraySize) ){
			for( auto token : Iterable<UA_UserTokenPolicy>(ep.userIdentityTokens, ep.userIdentityTokensSize) )
				tokens->second |= ToTokenType( token.tokenType );
		}
    UA_Array_delete( endpointArray, endpointArraySize, &UA_TYPES[UA_TYPES_ENDPOINTDESCRIPTION] );
    UA_Client_delete( client );
		return tokens->second;
	}
}