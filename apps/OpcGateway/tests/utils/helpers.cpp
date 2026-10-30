#include "helpers.h"
#include <thread>
#include <open62541/client_config_default.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/process/execution.h>
#include <jde/access/Authorize.h>
#include <jde/opc/uatypes/opcHelpers.h>
#include <jde/opc/UAException.h>
#include <jde/app/client/IAppClient.h>
#include <jde/web/Jwt.h>
#include "../../src/GatewayAppClient.h"
#include "../../src/UAClient.h"
#include "../../src/auth/OpcServerSession.h"
#include "../../src/auth/UM.h"
#include "../../src/ql/GatewayQL.h"
#include "GatewayClientSocket.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	α CreateServerCnnctnAwait::Execute()ι->QL::QLAwait<jobject>::Task{
		try{
			let certificateUri = _certificateUri ? *_certificateUri : OpcUrn();
			let url = _url ? *_url : OpcUrl();
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
		return BlockAwait( CreateServerCnnctnAwait{} );
	}

	α Tests::PurgeServerCnnctn( optional<ServerCnnctnPK> pk )ι->uint{
		return BlockAwait<PurgeServerCnnctnAwait,uint>( PurgeServerCnnctnAwait{pk} );
	}
	α Tests::PurgeServerCnnctn( str slug )ι->void{
		if( auto row = SelectServerCnnctn(slug); row )
			PurgeServerCnnctn( row->Id );
	}

	α Tests::SelectServerCnnctn( DB::Key id )ι->optional<ServerCnnctn>{
		let select = Ƒ( "serverConnection({}){{ id name attributes created updated deleted slug description certificateUri isDefault url securityMode securityPolicy sessionTimeout requestTimeout pingInterval ttl locale }}", id.QLInput() );
		auto o = QL().QuerySync<jobject>( select, id.QLVariables(), {UserPK::System} );
		return o.empty() ? optional<ServerCnnctn>{} : ServerCnnctn( move(o) );
	}

	α Tests::GetConnection( str slug )ε->ServerCnnctn{
		auto con = SelectServerCnnctn( {slug} );
		if( !con ){
			BlockAwait( ProviderMAwait{slug, false} );
			let id = BlockAwait( CreateServerCnnctnAwait{} );
			con = SelectServerCnnctn( id );
		}
		return *con;
	}
	α Tests::GetConnection( str slug, str url, str certificateUri )ε->ServerCnnctn{
		auto con = SelectServerCnnctn( {slug} );
		if( !con ){
			BlockAwait( ProviderMAwait{slug, false} );//a stale provider row goes first, as above - the create's hook inserts the slug's own.
			let id = BlockAwait( CreateServerCnnctnAwait{slug, url, certificateUri} );
			con = SelectServerCnnctn( id );
		}
		return *con;
	}

	static sp<Tests::GatewayClientSocket> _session;
	α Tests::Socket()ι->Tests::GatewayClientSocket&{
		if( !_session )
			_session = Tests::GatewayClientSocket::Open( "localhost", GatewayPort(), AppClient()->SessionId() );
		return *_session;
	}

	static optional<Web::Jwt> _jwt;
	α Tests::Jwt()ε->const Web::Jwt&{
		if( !_jwt )
			_jwt = BlockAwait<Web::Client::ClientSocketAwait<Web::Jwt>,Web::Jwt>( AppClient()->Jwt() );
		return *_jwt;
	}
	α Tests::SessionCredential( optional<UserPK> user )ε->Credential{
		Credential y{ Jwt().Payload() };
		if( user )
			y.SetUserPK( *user );
		return y;
	}

	α Tests::EnforceResource( sv schema, sv slug, bool on, sv criteria )ε->void{
		let app = AppClient();
		EnforceResource( *app, *app->Acl(), schema, slug, on, criteria );
	}
	α Tests::EnforceHistGroups( bool on )ε->void{
		EnforceResource( "gateway", "histGroups", false );
		if( !on )
			return;
		auto app = AppClient();
		GrantRole( *app, "GatewayHistGroupEditor", "gateway", "histGroups", {}, Access::ERights::All, app->UserPK() );
		EnforceResource( "gateway", "histGroups", true );
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
		GrantRole( *app, Ƒ("GatewayHistGroup{}", group), "gateway", "histGroups", criteria, allowed | Access::ERights::Administer, app->UserPK() );
		EnforceResource( "gateway", "histGroups", true, criteria );//a row is created unenforced.
	}
	α Tests::UnscopeHistGroup( uint32 group )ε->void{
		EnforceResource( "gateway", "histGroups", false, groupGuid(group) );//until then it still refuses what it grants no one, a purge of the group included
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

	static flat_map<string,ETokenType> _userTokens;
	α Tests::AvailableUserTokens( sv url_ )ε->ETokenType{
		str url{url_};
		if( _userTokens.contains(url) )
			return _userTokens[url];
		Logger logger;
		UA_ClientConfig config{};
		config.logging = &logger;
		config.tcpReuseAddr = true;
		UA_ClientConfig_setDefault( &config );
		auto UA_DateTime_now_fake = []( UA_EventLoop* )->UA_DateTime { return 0x5C8F735D; };
		config.eventLoop->dateTime_now = UA_DateTime_now_fake;
		config.eventLoop->dateTime_nowMonotonic = UA_DateTime_now_fake;
		UA_Client *client = UA_Client_newWithConfig( &config );
		UA_EndpointDescription* endpointArray{}; uint endpointArraySize{};
		auto tokens = _userTokens.try_emplace( url ).first;
		UAε( UA_Client_getEndpoints(client, url.c_str(), &endpointArraySize, &endpointArray) );
		for( auto ep : Iterable<UA_EndpointDescription>(endpointArray, endpointArraySize) ){
			for( auto token : Iterable<UA_UserTokenPolicy>(ep.userIdentityTokens, ep.userIdentityTokensSize) )
				tokens->second |= ToTokenType( token.tokenType );
		}
		UA_Array_delete( endpointArray, endpointArraySize, &UA_TYPES[UA_TYPES_ENDPOINTDESCRIPTION] );
		UA_Client_delete( client );
		return tokens->second;
	}

	α Tests::ReadValue( const sp<UAClient>& client, const NodeId& node )ε->Value{
		return BlockAwait( ReadValueAwait{{node}, client} ).at( node );
	}
	//By way of BaseDataType, which any value fits and which may hold none:  open62541 retypes a node only to a DataType its value
	//fits, and clears the value of a nullable one alone.
	Ω retype( UA_Server* ua, const NodeId& node, const UA_NodeId& dataType )ι->StatusCode{
		auto sc = UA_Server_writeDataType( ua, node, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATATYPE) );
		if( !sc )
			sc = UA_Server_writeDataValue( ua, node, Value{UA_STATUSCODE_BADWAITINGFORINITIALDATA} );
		return sc ? sc : UA_Server_writeDataType( ua, node, dataType );
	}
	Tests::RetypedNode::RetypedNode( UA_Server* ua, NodeId node, UA_UInt32 dataType )ε:
		_ua{ ua },
		_node{ move(node) },
		_value{ Opc::ReadValue(*ua, _node) }{
		UAε( UA_Server_readDataType(ua, _node, &_dataType) );
		if( let sc = retype(ua, _node, UA_NODEID_NUMERIC(0, dataType)); sc ){
			retype( ua, _node, _dataType );
			UA_Server_writeDataValue( ua, _node, _value );
			throw UAException{ sc };
		}
	}
	Tests::RetypedNode::~RetypedNode(){
		EXPECT_EQ( retype(_ua, _node, _dataType), UA_STATUSCODE_GOOD );
		EXPECT_EQ( UA_Server_writeDataValue(_ua, _node, _value), UA_STATUSCODE_GOOD );
	}
}