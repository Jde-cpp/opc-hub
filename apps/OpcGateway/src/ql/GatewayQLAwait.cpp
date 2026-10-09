#include "GatewayQLAwait.h"
#include <jde/fwk/co/AnyAwait.h>
#include <jde/ql/QLAwait.h>
#include <jde/opc/uatypes/BrowseName.h>
#include <jde/opc/uatypes/Variant.h>
#include "../GatewayAppClient.h"
#include "../async/CallAwait.h"
#include "../async/ReadValueAwait.h"
#include "../async/UAStrandAwait.h"
#include "../types/UAClientException.h"
#include "DataTypeQLAwait.h"
#include "HistEditQLAwait.h"
#include "HistQLAwait.h"
#include "NodeQLAwait.h"
#include "OpcSessionsQLAwait.h"
#include "SearchQLAwait.h"
#include "VariableQLAwait.h"
#define let const auto

namespace Jde::Opc::Gateway{
	Ω connectionAttribute( const sp<UAClient>& client, sv name )ε->UA_Variant{
		UA_Variant v{};
		if( let sc = UA_Client_getConnectionAttributeCopy(*client, BrowseName{name, 0}, &v); sc )
			throw UAClientException{ sc, client->Handle(), Ƒ("getConnectionAttribute('{}')", name) };
		return v;
	}

	α IGatewayQLAwait::GetClient( QL::IQLAwaitExe* await )ι->TAwait<sp<UAClient>>::Task{
		try{
			auto session = await->Session(); THROW_IF( !session, "No Session for query" );
			auto opcId = await->Input().As<jstring>( "opc" );
			_client = co_await ConnectAwait{ string{opcId}, *session };
			await->Query();
		}
		catch( runtime_error& e ){
			await->ResumeExp( move(e) );
		}
	}

	//Every server-bound query names its server with `opc` - GetClient reads that argument, so one without it could only ever
	//fail there.  Keyed on it (rather than "everything not listed") so a host QL that also serves app/access tables (OpcHub's
	//HubQL) can ask IsApplicable and keep `users`/`sessions`/`connections` off ConnectAwait.  __type(name:…) stays on the
	//generic QueryType; __type(opc:…) is an OPC enum DataType whose definition lives on that server (EnumTypeCache).
	Ω needsClient( const QL::Input& q )ι->bool{
		let tableName = q.JTableName();
		return q.Args.contains( "opc" ) && !tableName.starts_with( "serverConnection" ) && tableName!="status" && tableName!="opcSessions" && tableName!="search";//search must never ConnectAwait - it only reads clients already open.
	}
	α GatewayQLAwait::IsApplicable( const QL::TableQL& q )ι->bool{
		return q.JsonName=="opcSessions" || q.JsonName=="search" || ServerCnnctnSessionsQLAwait::IsApplicable(q) || needsClient(q);
	}
	α GatewayQLAwait::Test( QL::TableQL& q, QL::Creds executer, SL sl )->up<TAwait<jvalue>>{
		up<TAwait<jvalue>> await;
		if( q.JsonName=="opcSessions" )
			await = mu<OpcSessionsQLAwait>( move(q), move(executer), sl );
		else if( q.JsonName=="search" )
			await = mu<SearchQLAwait>( move(q), move(executer), sl );
		else if( ServerCnnctnSessionsQLAwait::IsApplicable(q) )
			await = mu<ServerCnnctnSessionsQLAwait>( move(q), move(executer), sl );
		else if( needsClient(q) )
			await = mu<GatewayQLAwait>( move(q), move(executer), sl );
		return await;
	}
	//A hist edit with `group` alone is the gateway historian's (Phase 5), which needs no client.  One that names both
	//`opc` and `group`, or neither, is this await's to refuse.
	α GatewayQLMAwait::IsApplicable( const QL::MutationQL& m )ι->bool{
		return m.JsonTableName=="variable" || ( HistQL::FindEdit(m.CommandName) && HistQL::Target(m)!=HistQL::ETarget::Group );
	}
	α GatewayQLMAwait::Suspend()ι->void{
		if( let edit = HistQL::FindEdit(_query.CommandName); edit && HistQL::Target(_query)==HistQL::ETarget::Neither )
			ResumeExp( HistQL::TargetRefused(*edit, _sl) );//before a client is opened for it.
		else
			GetClient( this );
	}
	α GatewayQLMAwait::Test( QL::MutationQL& m, QL::Creds executer, SL sl )->up<TAwait<jvalue>>{
		if( IsApplicable(m) )
			return mu<GatewayQLMAwait>( move(m), move(executer), sl );
		return nullptr;
	}

	α GatewayQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			jvalue y;
			_query.ReturnRaw = true;
			if( _query.JsonName.starts_with("node") || _query.JsonName.starts_with("variable") )
				y = co_await NodeQLAwait{ move(_query), move(_client), _sl };
			else if( _query.JsonName.starts_with("dataType") )
				y = co_await DataTypeQLAwait{ move(_query), move(_client), _sl };
			else if( _query.JsonName=="history" )//the server's own history, over the caller's session (spec *Pass-through*);  with `group` it never gets here.
				y = co_await HistQLAwait{ move(_query), move(_client), _sl };
			else if( _query.JsonName=="serverDescription" )//connection attributes are sync UA services - run them on the client's strand.
				y = co_await UAStrandAwait<jvalue>{ _client, [this]()->jvalue { return ServerDescription( move(_query), _client ); }, _sl };
			else if( _query.JsonName=="namespaces" ){//an ordinary read of a standard node - see Namespaces below.
				auto values = co_await Any( ReadValueAwait{{NodeId{(UA_UInt16)0, (UA_UInt32)UA_NS0ID_SERVER_NAMESPACEARRAY}}, _client, _sl} );
				y = Namespaces( move(_query), _client, move(values) );
			}
			else if( _query.JsonName=="__type" ){//an enumeration DataType's definition, read from the server on first use and cached with the client.
				NodeId id{ _query.Args };//ns/i|s|g|b;  `opc` is ignored.
				auto type = co_await _client->EnumTypes().Get( _client, move(id), _sl );//AnyAwait - legal from this TAwait<jvalue>::Task.
				y = type->ToJson( _query );
			}
			else if( _query.JsonName=="securityPolicyUri" )
				y = co_await UAStrandAwait<jvalue>{ _client, [this]()->jvalue { return SecurityPolicyUri( move(_query), _client ); }, _sl };
			else if( _query.JsonName=="securityMode" )
				y = co_await UAStrandAwait<jvalue>{ _client, [this]()->jvalue { return SecurityMode( move(_query), _client ); }, _sl };
			else
				throw Exception{ _sl, {}, "Unknown query type: {}", _query.JsonName };
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			TRACET( ELogTags::Test, "Exception in GatewayQLAwait::Query: {}", e.what() );
			ResumeExp( move(e) );
		}
	}

	α GatewayQLMAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			jvalue y;
			// if( m.Type==QL::EMutationQL::Execute )
			// 	results.push_back( co_await JCallAwait(move(m), _request.SessionInfo, _sl) );
			if( _query.DBTable && _query.TableName()=="server_connections" )
				y = co_await QL::QLAwait<>( move(_query), UserPK(), _sl );
			else if( _query.JsonTableName=="variable" ){
				auto session = Session();
				THROW_IF( !session, "No Session for mutation" );
				y = co_await VariableQLAwait{ move(_query), session, _sl };
			}
			else if( let edit = HistQL::FindEdit(_query.CommandName); edit )//the server's own history, over the caller's session (spec *Pass-through*).
				y = co_await HistEditQLAwait{ move(_query), *edit, move(_client), _sl };
			else
				throw Exception{ _sl, {},	"Unknown query type: {}", _query.JsonTableName };
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α GatewayQLAwait::ServerDescription( QL::TableQL&& q, sp<UAClient> client )ε->jobject{
		UA_Variant uaAttrib = connectionAttribute( client, "serverDescription" );
		let desc = ( UA_ApplicationDescription* )uaAttrib.data;
		jobject j;
		if( q.FindColumn("applicationUri") )
			j["applicationUri"] = ToString( desc->applicationUri );
		if( q.FindColumn("productUri") )
			j["productUri"] = ToString( desc->productUri );
		if( q.FindColumn("applicationName") )
			j["applicationName"] = ToString( desc->applicationName.text );
		if( q.FindColumn("applicationType") ){
			sv applicationType;
			switch( desc->applicationType ){
			case UA_ApplicationType::UA_APPLICATIONTYPE_SERVER:
				applicationType = "Server";
				break;
			case UA_ApplicationType::UA_APPLICATIONTYPE_CLIENT:
				applicationType = "Client";
				break;
			case UA_ApplicationType::UA_APPLICATIONTYPE_CLIENTANDSERVER:
				applicationType = "ClientAndServer";
				break;
			case UA_ApplicationType::UA_APPLICATIONTYPE_DISCOVERYSERVER:
				applicationType = "DiscoveryServer";
				break;
			case UA_ApplicationType::__UA_APPLICATIONTYPE_FORCE32BIT:
				ASSERT( false );
				break;
			}
			j["applicationType"] = applicationType;
		}
		if( q.FindColumn("gatewayServerUri") )
			j["gatewayServerUri"] = ToString( desc->gatewayServerUri );
		if( q.FindColumn("discoveryProfileUri") )
			j["discoveryProfileUri"] = ToString( desc->discoveryProfileUri );
		if( q.FindColumn("discoveryUrls") ){
			jarray discoveryUrls;
			for( size_t i=0; i<desc->discoveryUrlsSize; ++i )
				discoveryUrls.emplace_back( ToString(desc->discoveryUrls[i]) );
			j["discoveryUrls"] = discoveryUrls;
		}
		UA_Variant_clear( &uaAttrib );
		return q.TransformResult( move(j) );
	}
	//The server's own namespace array (the standard ns=0;i=2255 node), not UA_Client_getNamespaceUri:  open62541 fills its local
	//copy from an async read fired after the session activates, so a query right behind the connect sees an empty list.  The two
	//index the same anyway - the client appends the server's uris in order and the gateway predefines none of its own.
	α GatewayQLAwait::Namespaces( QL::TableQL&& q, sp<UAClient> client, flat_map<NodeId, Value>&& values )ε->jvalue{
		THROW_IF( values.empty(), "No result reading the namespace array." );
		let& value = values.begin()->second;
		if( value.hasStatus && value.status )
			throw UAClientException{ (StatusCode)value.status, client->Handle(), "read(Server_NamespaceArray)" };
		let& variant = value.value;
		THROW_IF( variant.type!=&UA_TYPES[UA_TYPES_STRING] || UA_Variant_isScalar(&variant), "The namespace array is a '{}'{}, not a string array.", variant.type ? variant.type->typeName : "null", UA_Variant_isScalar(&variant) ? " scalar" : "" );
		let wantIndex = q.FindColumn( "index" );
		let wantUri = q.FindColumn( "uri" );
		jarray y; y.reserve( variant.arrayLength );
		for( uint i=0; i<variant.arrayLength; ++i ){
			jobject o;
			if( wantIndex )
				o["index"] = i;
			if( wantUri )
				o["uri"] = ToString( ((UA_String*)variant.data)[i] );
			y.emplace_back( move(o) );
		}
		return q.TransformResult( move(y) );
	}
	α GatewayQLAwait::SecurityPolicyUri( QL::TableQL&& q, sp<UAClient> client )ε->jvalue{
		UA_Variant uaAttrib = connectionAttribute( client, "securityPolicyUri" );
		string uri = ToString( *(UA_String*)uaAttrib.data );
		UA_Variant_clear( &uaAttrib );
		return q.TransformResult( move(uri) );
	}
	α GatewayQLAwait::SecurityMode( QL::TableQL&& q, sp<UAClient> client )ε->jvalue{
		UA_Variant uaAttrib = connectionAttribute( client, "securityMode" );
		UA_MessageSecurityMode emode = *( UA_MessageSecurityMode* )uaAttrib.data;
		sv mode;
		switch( emode ){
		case UA_MESSAGESECURITYMODE_INVALID:
			mode = "Invalid";
			break;
		case UA_MESSAGESECURITYMODE_NONE:
			mode = "None";
			break;
		case UA_MESSAGESECURITYMODE_SIGN:
			mode = "Sign";
			break;
		case UA_MESSAGESECURITYMODE_SIGNANDENCRYPT:
			mode = "SignAndEncrypt";
			break;
		case __UA_MESSAGESECURITYMODE_FORCE32BIT:
			ASSERT( false );
			break;
		}
		UA_Variant_clear( &uaAttrib );
		return q.TransformResult( string{mode} );
	}
}