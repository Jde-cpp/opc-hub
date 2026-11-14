//Flows 8 (#454), the hub's shape:  the flow server inside the hub, on a port of this suite's own (Opc.Hub.Tests.jsonnet).  A
//flow's variable deployed through the hub's QL and collected by a group over the flow server's row:  the collector's
//session, which presents the process's pair, opens by the own-certificate rule as the user the hub enrolls for that
//certificate, and a web session's short issued token is answered from this process's session map, as a driver row's is
//(DriverTests).  The rest is the gateway suite's (apps/OpcGateway/tests/FlowServerTests.cpp).
#include <absl/cleanup/cleanup.h>
#include <jde/opc/uatypes/DateTime.h>
#include "../../OpcGateway/src/GatewayAppClient.h"
#include "../../OpcGateway/tests/utils/DriverRows.h"
#include "../../OpcGateway/src/flows/FlowServer.h"
#include "../../OpcGateway/src/hist/HistHost.h"
#include "helpers.h"
#define let const auto

namespace Jde::Opc::Hub::Tests{
	using namespace Gateway::Tests;
	using Gateway::Credential;
	using Gateway::FlowServer;
	using Gateway::HistHost;
	using Gateway::ServerCnnctn;
	using Gateway::UAClient;

	struct FlowServerTests : ::testing::Test{
	protected:
		Ω Mutate( sv ql, jobject vars={} )ε->jvalue{ return BlockAwait( QL::QLAwait<jvalue>{string{ql}, move(vars), Jde::UserPK{1}, Gateway::QLPtr()} ); }
		Ω Id( const jvalue& v )ε->uint32{ return Json::AsNumber<uint32>( Json::AsObject(v), "id" ); }
		Ω Sessions( sv type )ι->vector<Opc::Server::UAAccess::Session>{
			vector<Opc::Server::UAAccess::Session> y;
			if( let server = FlowServer::Instance() ){
				for( auto& session : server->Sessions() ){
					if( session.Type==type )
						y.push_back( move(session) );
				}
			}
			return y;
		}
		Ω Count( const Value& value )ι->optional<int>{ return value.hasValue && !value.status && UA_Variant_hasScalarType( &value.value, &UA_TYPES[UA_TYPES_INT32] ) ? optional<int>{ value.Get<UA_Int32>(0) } : nullopt; }
	};

	//The hub's shipped config reads, the gateway's flows block within it (the gateway suite's TheShippedConfigReads).
	TEST_F( FlowServerTests, TheShippedConfigReads ){
		let config = fs::path{ Process::GetEnv("REPO_SOURCE_DIR").value_or(".") }/"apps"/"OpcHub"/"config";
		let read = Json::TryReadJsonNet( config/"Opc.Hub.jsonnet", {config/"args"/"sqlite"}, {{"buildTarget", "debug"}, {"cwd", "."}, {"logsDir", "logs"}, {"windows", "false"}, {"path", ":memory:"}} );
		ASSERT_TRUE( read ) << read.error();
		let& flows = read->at( "gateway" ).as_object().at( "flows" ).as_object();
		EXPECT_TRUE( flows.contains("function") && flows.contains("server") ) << serialize( flows );
	}

	TEST_F( FlowServerTests, TheHubCollectsAFlowsVariable ){
		const string slug{ ServerCnnctn::FlowSlug }, flowSlug{ "hub-derived" };
		let server = FlowServer::Instance();
		ASSERT_TRUE( server ) << "the hub's flow server never started";
		EXPECT_EQ( server->Port(), 4977 ) << "the suite's own, clear of a live hub's 4860 and the gateway suite's 4960";
		let row = SelectServerCnnctn( {slug} );
		ASSERT_TRUE( row ) << "the flow server's row";
		EXPECT_EQ( row->Kind, ServerCnnctn::KindFlow );
		EXPECT_EQ( row->Url, "opc.tcp://127.0.0.1:4977" );

		let flow = Id( Mutate("createFlow( name:$name, slug:$slug, enabled:true ){ id }", {{"name", "Hub derived"}, {"slug", flowSlug}}) );
		absl::Cleanup purge = [=]{ try{ Mutate( "purgeFlow( id:$id )", {{"id", flow}} ); }catch( const std::exception& e ){ ADD_FAILURE() << "purgeFlow:  " << e.what(); } };
		let graph = jobject{ {"nodes", jarray{
			jobject{{"id", "in"}, {"type", "inject"}, {"props", jobject{}}, {"wires", jarray{jarray{"count"}}}},
			jobject{{"id", "count"}, {"type", "variable"}, {"props", jobject{{"name", "count"}, {"dataType", "Int32"}}}, {"wires", jarray{}}}}} };
		let deployed = Mutate( "deployFlow( flow:$flow, graph:$graph, comment:$comment ){ running fault{ node message } }", {{"flow", flow}, {"graph", graph}, {"comment", nullptr}} );
		ASSERT_EQ( deployed.as_object().at("running"), true ) << serialize( deployed );
		let inject = [&]( int count ){ Mutate( "injectFlow( flow:$flow, node:$node, message:$message ){ node }", {{"flow", flow}, {"node", "in"}, {"message", jobject{{"payload", count}}}} ); };
		inject( 7 );

		let variable = FlowServer::VariableId( flowSlug, "count" );
		let collector = HistHost::Instance().Collector( slug );//the process's pair, for its channel and its token alike.
		ASSERT_TRUE( WaitUntil([&]{
			try{
				return Count( ReadValue(BlockAwait(UAClient::GetClient(slug, collector)), variable) )==7;
			}
			catch( const std::exception& ){
				return false;
			}
		}, 15s) ) << "the collector never read the variable";
		let own = Sessions( "Certificate" );
		ASSERT_EQ( own.size(), 1u );
		let client = Gateway::AppClient();
		let key = Crypto::ExtractPublicKey( Crypto::ReadCertificate(client->SslSettings->Certificate.Path) );
		let enrolled = client->QuerySync( Ƒ("user( modulus: \"{}\", exponent: {}, deleted: null ){{id}}", key.ModulusHex(), key.ExponentInt()), {} );
		ASSERT_FALSE( enrolled.empty() ) << "the hub enrolls its certificate at its start";
		EXPECT_EQ( own.front().User, Jde::UserPK{QL::AsId<Jde::UserPK::Type>(enrolled)} ) << "the certificate's enrolled user";
		EXPECT_NE( own.front().User, Jde::UserPK{Jde::UserPK::System} ) << "not the hub's own";

		//A group over the row:  the hub's collector stores what the flow writes.
		let group = Id( Mutate("createHistGroup( name:$name, slug:$slug, serverConnectionId:$connection ){ id }", {{"name", "Hub variables"}, {"slug", "hub-variables"}, {"connection", row->Id}}) );
		absl::Cleanup purgeGroup = [=]{ try{ Mutate( "purgeHistGroup( id:$id )", {{"id", group}} ); }catch( const std::exception& e ){ ADD_FAILURE() << "purgeHistGroup:  " << e.what(); } };
		let member = Id( Mutate("createHistGroupNode( histGroupId:$group, node:$node ){ id }", {{"group", group}, {"node", FlowServer::NodeText(flowSlug, "count")}}) );
		let host = HistHost::Instance().Find( group );
		ASSERT_TRUE( host ) << "the group is live";
		ASSERT_TRUE( WaitUntil([&]{ return host->Subscribed()==1; }, 15s) ) << "the collector never subscribed to the variable";
		let since = UADateTime{ Clock::now()-1h }.UA();
		let stored = [&]( int count ){
			for( let& read : host->Group()->Read({.Nodes={member}, .Start=since, .Limit=100}).Values ){
				if( read.Value.has_value() && Count(Hist::ToUA(read.Value))==count )
					return true;
			}
			return false;
		};
		inject( 8 );
		EXPECT_TRUE( WaitUntil([&]{ return stored( 8 ); }, 15s) ) << "the group never stored the flow's value";

		WebSession session;//a hub session as the page holds one;  its id in hex is the token the gateway's clients present.
		Credential token{ session.Authorization };
		EXPECT_EQ( OpCount(ReadValue(BlockAwait(UAClient::GetClient(slug, token)), FlowServer::FlowId(flowSlug, "Version"))), 1u );
		let web = Sessions( "IssuedToken" );
		ASSERT_EQ( web.size(), 1u );
		EXPECT_EQ( web.front().User, Jde::UserPK{1} ) << "the session's user, by the hub's own lookup";
	}
}