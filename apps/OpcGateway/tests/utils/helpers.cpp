#include "helpers.h"
#include <thread>
#include <jde/fwk/settings.h>
#include <jde/access/Authorize.h>
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
			str url = _url ? *_url : string{ Settings::FindSV("/opc/url").value_or("opc.tcp://127.0.0.1:4940") };
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
	α Tests::EnforceHistGroups( bool on )ε->void{
		auto app = AppClient();
		let slug = jobject{ {"slug", "histGroups"}, {"schema", "gateway"} };
		let acl = app->Acl();
		let reached = [&]( bool active, sv change ){
			for( uint i=0; acl->FindActiveResourcePK("gateway", "histGroups", "").has_value()!=active; ++i ){
				THROW_IF( i==200, "The histGroups resource's {} didn't reach the gateway's authorizer.", change );
				std::this_thread::sleep_for( std::chrono::milliseconds{50} );
			}
		};
		app->QuerySync<jvalue>( "deleteResource( schemaName:$schema, slug:$slug, criteria:null )", slug );
		reached( false, "delete" );
		if( !on )
			return;
		constexpr sv roleSlug{ "GatewayHistGroupEditor" };
		if( app->QuerySync("role(slug:$slug, deleted:null){id}", {{"slug", roleSlug}}).empty() ){
			let role = app->QuerySync<jobject>( "createRole( slug:$slug, name:$name ){id}", {{"slug", roleSlug}, {"name", "Gateway group history editor"}} );
			let roleId = Json::AsNumber<Access::RolePK::Type>( role.at("id") );
			app->QuerySync<jvalue>( "addRole( id:$roleId, permissionRight:{allowed:$allowed, denied:0, resource:{schemaName:\"gateway\", slug:\"histGroups\"}} )", {{"roleId", roleId}, {"allowed", underlying(Access::ERights::All)}} );
			app->QuerySync<jvalue>( "createAcl( identity:{ id:$userId }, role:{id:$roleId} )", {{"userId", app->UserPK().Value}, {"roleId", roleId}} );
		}
		app->QuerySync<jvalue>( "restoreResource( schemaName:$schema, slug:$slug, criteria:null )", slug );
		reached( true, "restore" );
	}
	//The guid of a group's row, the criteria of its own access resource (spec *Authorization*):  the gateway's table, as the system.
	Ω groupGuid( uint32 group )ε->string{
		let rows = QL().QuerySync<jarray>( "histGroups( id:$id ){ guid }", {{"id", group}}, UserPK{UserPK::System} );
		THROW_IF( rows.size()!=1, "Group {} has {} hist_groups rows.", group, rows.size() );
		return Json::AsString( rows[0].as_object(), "guid" );
	}
	α Tests::ScopeHistGroup( uint32 group, Access::ERights allowed )ε->void{
		let app = AppClient();
		THROW_IF( !app->Acl()->FindActiveResourcePK("gateway", "histGroups", ""), "Group {}'s resource would count for nothing:  histGroups' root isn't enforced (EnforceHistGroups).", group );
		let criteria = groupGuid( group );
		app->QuerySync<jvalue>( "createResource( schemaName:\"gateway\", name:$name, slug:\"histGroups\", criteria:$criteria, allowed:255 )", {{"name", Ƒ("histGroups[{}]", group)}, {"criteria", criteria}} );
		let role = app->QuerySync<jobject>( "createRole( slug:$slug, name:$name ){id}", {{"slug", Ƒ("GatewayHistGroup{}", group)}, {"name", Ƒ("Gateway history group {}", group)}} );
		let roleId = Json::AsNumber<Access::RolePK::Type>( role.at("id") );
		app->QuerySync<jvalue>( "addRole( id:$roleId, permissionRight:{allowed:$allowed, denied:0, resource:{schemaName:\"gateway\", slug:\"histGroups\", criteria:$criteria}} )", {{"roleId", roleId}, {"allowed", underlying(allowed | Access::ERights::Administer)}, {"criteria", criteria}} );
		app->QuerySync<jvalue>( "createAcl( identity:{ id:$userId }, role:{id:$roleId} )", {{"userId", app->UserPK().Value}, {"roleId", roleId}} );
		app->QuerySync<jvalue>( "restoreResource( schemaName:\"gateway\", slug:\"histGroups\", criteria:$criteria )", {{"criteria", criteria}} );//a row is created unenforced.
		for( uint i=0; !app->Acl()->FindActiveResourcePK("gateway", "histGroups", criteria); ++i ){//the gateway's authorizer hears of it over its subscription.
			THROW_IF( i==200, "Group {}'s resource didn't reach the gateway's authorizer.", group );
			std::this_thread::sleep_for( std::chrono::milliseconds{50} );
		}
	}
	α Tests::UnscopeHistGroup( uint32 group )ε->void{
		let app = AppClient();
		let criteria = groupGuid( group );
		app->QuerySync<jvalue>( "deleteResource( schemaName:\"gateway\", slug:\"histGroups\", criteria:$criteria )", {{"criteria", criteria}} );
		for( uint i=0; app->Acl()->FindActiveResourcePK("gateway", "histGroups", criteria); ++i ){//until then it still refuses what it grants no one, a purge of the group included
			THROW_IF( i==200, "Group {}'s resource deletion didn't reach the gateway's authorizer.", group );
			std::this_thread::sleep_for( std::chrono::milliseconds{50} );
		}
	}

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