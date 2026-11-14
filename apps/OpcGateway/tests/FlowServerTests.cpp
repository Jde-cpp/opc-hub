//Flows 8 (#454):  the flow server (docs/flows/spec.md *Derived variables*) - its row, the gateway's own and held to its
//kind's rule;  a `variable` node's props read at the deploy;  a flow's folder browsed through the gateway, `opc in` on
//the pumps' motorRpm through a Function body into a `variable`, the variable read, grouped and read back through
//`history`, a write to it refused, `_Flow` counting, and the folder following the deploys, the flow's row and its purge.
//The row's testConnection, and a server that could not listen tried again.  The committed replay is FlowReplayTests'.
//And the shipped config, read as the gateway's start reads it.
#include <jde/opc/ServerTrust.h>
#include "utils/ClientFixture.h"
#include "utils/DriverRows.h"
#include "utils/FlowFixture.h"

namespace Jde::Opc::Gateway::Tests{
	struct FlowServerTests : FlowFixture{
	protected:
		Ω Server()ε->sp<FlowServer>{
			auto server = FlowServer::Instance();
			THROW_IF( !server, "The flow server is not running." );
			return server;
		}
		Ω Sessions( sv type )ε->vector<Opc::Server::UAAccess::Session>{
			vector<Opc::Server::UAAccess::Session> y;
			for( auto& session : Server()->Sessions() ){
				if( session.Type==type )
					y.push_back( move(session) );
			}
			return y;
		}
		//A variable's value as a client reads it, its status with it.
		Ω VariableValue( sv flow, sv name )ε->Value{ return ReadValue( BlockAwait(UAClient::GetClient(FlowsSlug(), SessionCredential())), FlowServer::VariableId(flow, name) ); }
		//`_Flow`'s Version, Messages or Errors, as a client reads it:  the socket spells a UInt64 as a Long.
		Ω Total( sv flow, sv name )ε->uint{
			let count = OpCount( ReadValue(BlockAwait(UAClient::GetClient(FlowsSlug(), SessionCredential())), FlowServer::FlowId(flow, name)) );
			THROW_IF( !count, "{}'s _Flow.{} is no count.", flow, name );
			return *count;
		}
		//Whether a browse finds the flow's folder under Objects, and a variable under its flow's folder.
		Ω Listed( sv flow )ε->bool{ return std::ranges::contains( Children(NodeId::ObjectsFolder()), flow ); }
		Ω Listed( sv flow, sv name )ε->bool{ return std::ranges::contains( Children(FlowServer::FolderId(flow)), name ); }
	};

	//The shipped config reads:  its flows block's sandbox key was once bare, `function`, which is jsonnet's keyword, and no
	//suite read the file.
	TEST_F( FlowServerTests, TheShippedConfigReads ){
		let config = fs::path{ Process::GetEnv("REPO_SOURCE_DIR").value_or(".") }/"apps"/"OpcGateway"/"config";
		let read = Json::TryReadJsonNet( config/"Opc.Gateway.jsonnet", {config/"args"/"sqlite"}, {{"buildTarget", "debug"}, {"cwd", "."}, {"logsDir", "logs"}, {"windows", "false"}, {"path", ":memory:"}} );
		ASSERT_TRUE( read ) << read.error();
		let& flows = read->at( "gateway" ).as_object().at( "flows" ).as_object();
		EXPECT_TRUE( flows.contains("function") && flows.contains("server") ) << serialize( flows );
	}

	//The row is there from the start, with what the kind fixes supplied, and its server listens where the block says.  No
	//other row takes the kind, and this one keeps it, its url and its channel.
	TEST_F( FlowServerTests, TheRowIsTheGatewaysOwn ){
		let row = FlowsRow();
		EXPECT_EQ( row.Kind, ServerCnnctn::KindFlow );
		EXPECT_EQ( row.Url, "opc.tcp://127.0.0.1:4960" ) << "the suite's, clear of the shipped 4860";
		EXPECT_EQ( Server()->Port(), 4960 );
		EXPECT_FALSE( row.CertificateUri.empty() ) << "the gateway's own applicationUri, supplied";
		EXPECT_EQ( row.DefaultBrowseNs, ServerCnnctn::DriverBrowseNs );
		EXPECT_FALSE( row.HasDriver() ) << "no driver host starts for it";
		EXPECT_FALSE( DriverHost::Find(row.Slug) );

		let namespaces = ReadValue( BlockAwait(UAClient::GetClient(row.Slug, SessionCredential())), NodeId{0, UA_NS0ID_SERVER_NAMESPACEARRAY} );
		ASSERT_EQ( namespaces.value.arrayLength, 3u );
		EXPECT_EQ( ToString(namespaces.Get<UA_String>(2)), "urn:jde:flows:flows" );
		let web = Sessions( "IssuedToken" );
		ASSERT_FALSE( web.empty() ) << "the suite's session, by issued token";
		for( let& session : web ){//one, and any a client a test removed is still closing.
			EXPECT_EQ( session.User, AppClient()->UserPK() );
			EXPECT_EQ( session.Mode, UA_MESSAGESECURITYMODE_SIGNANDENCRYPT );
		}

		let other = CreateRow( "flows-too", "url:$url, kind:\"flow\"", {{"url", row.Url}} );
		ASSERT_FALSE( other ) << "a second row of the kind was made";
		EXPECT_TRUE( Has(other.error(), "is the flow server's connection alone, 'flows'") ) << other.error();
		let refused = [&]( sv args, jobject variables, sv why ){
			let error = UpdateRow( row.Id, args, move(variables) );
			EXPECT_TRUE( error && Has(*error, why) ) << args << ":  " << error.value_or( "saved" );
		};
		refused( "url:$url", {{"url", "opc.tcp://127.0.0.1:4940"}}, "is not the flow server's, opc.tcp://127.0.0.1:4960" );
		refused( "kind:\"ua\"", {}, "Slug 'flows' is the flow server's connection, of kind 'flow'" );
		refused( "certificateUri:\"urn:other\"", {}, "takes no certificateUri:  its clients connect to the gateway's own flow server" );
		refused( "securityMode:\"None\"", {}, "takes no securityMode" );
		refused( "driver:\"{}\"", {}, "takes no driver settings" );
		EXPECT_EQ( UpdateRow(row.Id, "description:\"The flows' derived variables.\""), nullopt ) << "what the kind does not fix is the operator's";
		EXPECT_EQ( FlowsRow().Url, row.Url );
	}

	//The row's Test Connection, as the connection page sends it, with no kind:  the flow server, as the row's clients dial it.
	TEST_F( FlowServerTests, TheRowIsTestedAsItsClientsDialIt ){
		let tested = Socket().QuerySync( "testConnection( opc: $opc, url: $url ){ applicationUri securityMode namespaces{ index uri } }", {{"opc", FlowsSlug()}, {"url", FlowsRow().Url}} ).as_object();
		EXPECT_EQ( Json::AsSV(tested, "securityMode"), "SignAndEncrypt" ) << serialize( tested );
		let& namespaces = Json::AsArray( tested, "namespaces" );
		ASSERT_EQ( namespaces.size(), 3u ) << serialize( tested );
		EXPECT_EQ( Json::AsSV(namespaces[2].as_object(), "uri"), FlowServer::NamespaceUri() );
		EXPECT_FALSE( UAClient::ConnectErrors().contains(FlowsSlug()) );
	}

	TEST_F( FlowServerTests, AVariablesPropsAreReadAtTheDeploy ){
		let flow = CreateFlow( "variable-props" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		let refused = [&]( sv props ){ return Refused( [&]{ Deploy( flow, Graph({N("in", "inject", "{}", {{"n"}}), N("n", "variable", props)}) ); } ); };
		let none = refused( "{}" );
		EXPECT_EQ( none.Status, EHttpStatus::BadRequest );
		EXPECT_TRUE( none.What.starts_with("Node 'n':  ") && Has(none.What, "`name` must be text") ) << none.What;
		EXPECT_TRUE( Has(refused(R"({"name":"mean"})").What, "`dataType` must be text") );
		EXPECT_TRUE( Has(refused(R"({"name":"mean","dataType":"Variant"})").What, "`dataType` 'Variant' is no built-in type a flow writes") );
		EXPECT_TRUE( Has(refused(R"({"name":"mean","dataType":"Double[][]"})").What, "is no built-in type a flow writes") );
		EXPECT_TRUE( Has(refused(R"({"name":"pump1/mean","dataType":"Double"})").What, "holds a '/'") );
		EXPECT_TRUE( Has(refused(R"({"name":"_Flow","dataType":"Double"})").What, "is the folder's own object") );
		EXPECT_TRUE( Has(refused(R"({"name":"mean","dataType":"Double","euRange":{"low":10,"high":10}})").What, "`euRange`'s low must be under its high") );
		EXPECT_TRUE( Has(refused(R"({"name":"mean","dataType":"Double","engineeringUnits":7})").What, "`engineeringUnits` must be the unit's text") );
		EXPECT_TRUE( Has(refused(R"({"name":"mean","dataType":"Double","engineeringUnits":""})").What, "`engineeringUnits` must be the unit's text") ) << "as an empty description is refused";
		let twice = Refused( [&]{ Deploy( flow, Graph({N("in", "inject", "{}", {{"a", "b"}}), N("a", "variable", R"({"name":"mean","dataType":"Double"})"), N("b", "variable", R"({"name":"mean","dataType":"Int32"})")}) ); } );
		EXPECT_EQ( twice.Status, EHttpStatus::BadRequest );
		EXPECT_TRUE( twice.What.starts_with("Node 'b':  ") && Has(twice.What, "Variable 'mean' is node 'a's") ) << twice.What;
		EXPECT_TRUE( Versions(flow).empty() ) << "a refusal writes nothing";
		EXPECT_FALSE( Listed("variable-props") ) << "and serves nothing";
		EXPECT_EQ( Deploy(flow, Graph({N("in", "inject", "{}", {{"n"}}), N("n", "variable", R"({"name":"mean","dataType":"double"})")})).at("running"), true ) << "a type's name in any case";
		EXPECT_TRUE( Listed("variable-props") && Listed("variable-props", "mean") );
	}

	//`opc in` on the pumps' motorRpm through a Function body into a `variable`:  the flow's folder browsed through the
	//gateway, the variable's value the body's, a group over the flow server's row storing it, a write refused, and `_Flow`
	//counting.  Then the folder as the deploys, the row and the purge leave it.
	TEST_F( FlowServerTests, AVariableIsANodeOfTheFlowServer ){
		const string slug{ "derived" };
		let flow = CreateFlow( slug );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		let doubled = N( "v", "variable", R"({"name":"doubled","dataType":"Double","description":"Twice the pump's rpm","engineeringUnits":"rpm","euRange":{"low":0,"high":6000}})" );
		let idle = N( "idle", "variable", R"({"name":"idle","dataType":"Int32[]"})" );
		let graph = Graph( {N("src", "opc in", On(Ex(RpmManual)), {{"f"}}), N("f", "function", R"({"code":"msg.payload = msg.payload*2;\nreturn msg;"})", {{"v"}}), doubled, N("in", "inject", "{}", {{"idle"}}), idle} );
		ASSERT_EQ( DeployBySession(flow, graph).at("running"), true );

		let folder = Socket().QuerySync( "node( opc: $opc, path: $path ){ id name nodeClass }", {{"opc", FlowsSlug()}, {"path", slug}} );//a bare segment is the row's namespace's.
		EXPECT_EQ( NodeId{ExNodeId{folder}.nodeId}.ToString(), FlowServer::FolderId(slug).ToString() );
		EXPECT_EQ( Json::FindNumber<uint>(folder.as_object(), "nodeClass"), UA_NODECLASS_OBJECT );
		EXPECT_EQ( Children(FlowServer::FolderId(slug)), (vector<string>{"_Flow", "doubled", "idle"}) );
		EXPECT_EQ( Children(FlowServer::VariableId(slug, "_Flow")), (vector<string>{"Enabled", "Errors", "Messages", "Version"}) );
		EXPECT_EQ( Children(FlowServer::VariableId(slug, "doubled")), (vector<string>{"EURange", "EngineeringUnits"}) );
		let attributes = FlowsNode( FlowServer::VariableId(slug, "doubled"), "accessLevel userAccessLevel description dataType valueRank" );
		EXPECT_EQ( Json::FindNumber<uint>(attributes, "accessLevel"), optional<uint>{UA_ACCESSLEVELMASK_READ} ) << serialize( attributes );
		EXPECT_EQ( Json::FindNumber<uint>(attributes, "userAccessLevel"), optional<uint>{UA_ACCESSLEVELMASK_READ} );
		let range = Socket().QuerySync( "node( opc: $opc, path: $path ){ value }", {{"opc", FlowsSlug()}, {"path", "derived/doubled/EURange"}} ).as_object().at( "value" );
		EXPECT_EQ( Json::FindNumber<double>(range.as_object(), "High"), 6000.0 ) << serialize( range );

		let waiting = VariableValue( slug, "idle" );
		EXPECT_EQ( waiting.status, UA_STATUSCODE_BADWAITINGFORINITIALDATA ) << "until its flow writes it";
		Inject( flow, "in", jobject{{"payload", nullptr}, {"status", UA_STATUSCODE_BADNOCOMMUNICATION}} );
		ASSERT_TRUE( WaitUntil([&]{ return VariableValue( slug, "idle" ).status==UA_STATUSCODE_BADNOCOMMUNICATION; }) ) << "a payload of none under a Bad status is the status";
		Inject( flow, "in", jobject{{"payload", jarray{1, 2, 3}}} );
		ASSERT_TRUE( WaitUntil([&]{ return Served( slug, "idle" )==jvalue( jarray{1, 2, 3} ); }) ) << serialize( Served(slug, "idle") );
		Inject( flow, "in", jobject{{"payload", 4}} );
		ASSERT_TRUE( Errors(flow, "idle", 1) ) << "a scalar is no array";
		EXPECT_TRUE( WaitUntil([&]{ return Flows().Status( flow, "idle" ).Text.contains( "`payload` is no array, and variable 'idle' is Int32[]" ); }) ) << Flows().Status( flow, "idle" ).Text;

		let base = BaseRpm();
		WriteRpm( base, Since() );
		ASSERT_TRUE( WaitUntil([&]{ return Served( slug, "doubled" )==jvalue{ base*2 }; }) ) << serialize( Served(slug, "doubled") );
		EXPECT_EQ( VariableValue(slug, "doubled").status, UA_STATUSCODE_GOOD );

		auto held = GroupVariable( "derived", slug, "doubled" );
		absl::Cleanup purgeGroup = [&]{ held.Host = nullptr; Purge( held.Id, Items() ); };
		let group = held.Id;
		ASSERT_TRUE( WaitUntil([&]{ return Sessions( "Certificate" ).size()==1; }) ) << "the collector's, by the process's own pair";
		EXPECT_EQ( Sessions("Certificate").front().User, AppClient()->UserPK() ) << "the own-certificate rule:  the gateway's own user, with no lookup";
		WaitStored( held.Host, held.Member, Joined, 1 );
		let second = Since();
		WriteRpm( base+1000, second );
		ASSERT_TRUE( WaitUntil([&]{ return ReadAll( Group(group), jarray{Variable(slug, "doubled")}, Request{.Start=second, .End=second+1s} ).size()==1; }) ) << "the collector stored the variable's change";
		let read = ReadAll( Group(group), jarray{Variable(slug, "doubled")}, Request{.Start=second, .End=second+1s} ).front();
		EXPECT_EQ( read.Value, jvalue{(base+1000)*2} ) << "the body's value, through history";
		EXPECT_EQ( read.Source, optional<TimePoint>{second} ) << "the message's source time, its data change's";
		EXPECT_EQ( read.Status, UA_STATUSCODE_GOOD );

		let written = Refused( [&]{ Socket().QuerySync( "updateVariable( opc: $opc, id: $id, value: $value ){ value }", {{"opc", FlowsSlug()}, {"id", FlowServer::VariableId(slug, "doubled").ToJson()}, {"value", 1.5}} ); } );
		EXPECT_TRUE( Has(written.What, "BadNotWritable") ) << "a write to a flow's variable:  " << written.What;
		EXPECT_EQ( Served(slug, "doubled"), jvalue{(base+1000)*2} ) << "and changed nothing";

		EXPECT_EQ( Total(slug, "Version"), 1u );
		EXPECT_EQ( Served(FlowServer::FlowId(slug, "Enabled")), jvalue{true} );
		let messages = Total( slug, "Messages" );
		EXPECT_GE( messages, 5u ) << "three injects and two data changes at the least";
		EXPECT_EQ( Total(slug, "Errors"), 1u );
		WriteRpm( base+2000, Since() );
		ASSERT_TRUE( WaitUntil([&]{ return Total( slug, "Messages" )==messages+1; }) ) << Total( slug, "Messages" );

		//A deploy keeps a variable by its name, with its value, and one its graph lacks leaves the folder.
		let writes = Server()->Writes();
		ASSERT_EQ( Deploy(flow, Graph({N("in", "inject", "{}", {{"idle"}}), N("idle", "variable", R"({"name":"idle","dataType":"Int32[]","description":"Kept","euRange":{"low":0,"high":9}})")})).at("running"), true );
		EXPECT_EQ( Children(FlowServer::FolderId(slug)), (vector<string>{"_Flow", "idle"}) ) << "the variable the deploy dropped is browsed no more";
		EXPECT_EQ( VariableValue(slug, "doubled").status, UA_STATUSCODE_BADOBJECTDELETED ) << "and says so to who holds its id";
		EXPECT_EQ( Served(slug, "idle"), jvalue(jarray{1, 2, 3}) ) << "its value kept across the versions";
		EXPECT_EQ( Children(FlowServer::VariableId(slug, "idle")), vector<string>{"EURange"} ) << "a property it gained";
		EXPECT_EQ( Server()->Writes(), writes );
		EXPECT_EQ( Total(slug, "Version"), 2u );
		EXPECT_EQ( Total(slug, "Messages"), 0u ) << "a run's own";
		//One whose type changes is its node still.
		ASSERT_EQ( Deploy(flow, Graph({N("in", "inject", "{}", {{"idle"}}), N("idle", "variable", R"({"name":"idle","dataType":"String"})")})).at("running"), true );
		EXPECT_EQ( VariableValue(slug, "idle").status, UA_STATUSCODE_BADWAITINGFORINITIALDATA );
		EXPECT_TRUE( Children(FlowServer::VariableId(slug, "idle")).empty() ) << "and a property it lost";
		Inject( flow, "in", jobject{{"payload", "running"}, {"status", UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE}} );
		ASSERT_TRUE( WaitUntil([&]{ return OpText( VariableValue(slug, "idle") )=="running"; }) );
		EXPECT_EQ( VariableValue(slug, "idle").status, UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE ) << "the message's status";
		//A deploy that brings a variable back brings its node back, and the group's item, which never left it, hears it again.
		ASSERT_EQ( DeployBySession(flow, graph).at("running"), true );
		EXPECT_EQ( Children(FlowServer::FolderId(slug)), (vector<string>{"_Flow", "doubled", "idle"}) );
		EXPECT_EQ( Children(FlowServer::VariableId(slug, "doubled")), (vector<string>{"EURange", "EngineeringUnits"}) );
		Inject( flow, "in", jobject{{"payload", jarray{4}}} );
		ASSERT_TRUE( WaitUntil([&]{ return Served( slug, "idle" )==jvalue( jarray{4} ); }) ) << "an array's again";
		let third = Since();
		WriteRpm( base+3000, third );
		ASSERT_TRUE( WaitUntil([&]{ return ReadAll( Group(group), jarray{Variable(slug, "doubled")}, Request{.Start=third, .End=third+1s} ).size()==1; }) ) << "the collector, on the item it had, stored the variable's change";
		EXPECT_EQ( ReadAll(Group(group), jarray{Variable(slug, "doubled")}, Request{.Start=third, .End=third+1s}).front().Value, jvalue{(base+3000)*2} );

		Mutate( "updateFlow( id:$id, enabled:false )", {{"id", flow}} );
		ASSERT_TRUE( WaitUntil([&]{ return Served( FlowServer::FlowId(slug, "Enabled") )==jvalue{ false }; }) );
		EXPECT_TRUE( Listed(slug) && Listed(slug, "idle") ) << "a disabled flow keeps its folder";
		Mutate( "updateFlow( id:$id, enabled:true )", {{"id", flow}} );
		ASSERT_TRUE( WaitUntil([&]{ return Running( flow ); }) );
		//With no variable left the folder goes, and a purge takes one that is there.
		ASSERT_EQ( Deploy(flow, Plain()).at("running"), true );
		EXPECT_FALSE( Listed(slug) ) << "a flow with no variable has no folder";
		ASSERT_EQ( Deploy(flow, Graph({N("in", "inject", "{}", {{"idle"}}), idle})).at("running"), true );
		EXPECT_TRUE( Listed(slug) && Listed(slug, "idle") );
		PurgeFlow( flow );
		std::move( purge ).Cancel();
		EXPECT_TRUE( WaitUntil([&]{ return !Listed( slug ); }) ) << "the purge took the folder";
		//A flow made under the purged one's slug has the folder, and its own figures.
		let next = CreateFlow( slug );
		absl::Cleanup purgeNext = [=]{ PurgeFlow( next ); };
		ASSERT_EQ( Deploy(next, Graph({N("in", "inject", "{}", {{"idle"}}), idle})).at("running"), true );
		EXPECT_EQ( Children(FlowServer::FolderId(slug)), (vector<string>{"_Flow", "idle"}) );
		EXPECT_EQ( VariableValue(slug, "idle").status, UA_STATUSCODE_BADWAITINGFORINITIALDATA ) << "not the purged flow's value";
		Inject( next, "in", jobject{{"payload", jarray{5}}} );
		ASSERT_TRUE( WaitUntil([&]{ return Total( slug, "Messages" )==1; }) ) << "the new flow's count";
		EXPECT_EQ( Total(slug, "Version"), 1u );
	}

	//The row's clients trust the flow server by the process's own certificate, anchored beside /gateway/trustedCertDirs, as a
	//driver row's do:  with no directory at all a client of the row connects.
	TEST_F( FlowServerTests, ItsClientsTrustItByTheAnchor ){
		let cred = SessionCredential();
		if( auto held = UAClient::Find(FlowsSlug(), cred) )//one a test before this connected, under the suite's list.
			UAClient::RemoveClient( move(held) );
		ServerTrust::OverrideTrustedCertDirs( vector<fs::path>{} );//the suite's own list holds the process's certificate, and would hide the anchor.
		absl::Cleanup restore = []{ ServerTrust::OverrideTrustedCertDirs( nullopt ); };
		auto client = TryConnect( FlowsSlug(), cred );
		ASSERT_TRUE( client ) << client.error()->what();
		let namespaces = ReadValue( *client, NodeId{0, UA_NS0ID_SERVER_NAMESPACEARRAY} );
		ASSERT_EQ( namespaces.value.arrayLength, 3u );
		EXPECT_EQ( ToString(namespaces.Get<UA_String>(2)), FlowServer::NamespaceUri() );
		UAClient::RemoveClient( move(*client) );
	}

	//A server that could not listen, the port another holds, is tried again by the host, which starts the flow that waited for
	//it, its folder as its current version has it.
	TEST_F( FlowServerTests, AServerThatCouldNotListenIsTriedAgain ){
		const string slug{ "relisten" };
		let flow = CreateFlow( slug );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		ASSERT_EQ( Deploy(flow, Graph({N("in", "inject", "{}", {{"v"}}), N("v", "variable", R"({"name":"mean","dataType":"Double"})")})).at("running"), true );
		let port = Server()->Port();
		for( let& cred : {SessionCredential(), HistHost::Instance().Collector(FlowsSlug())} ){//as at a shutdown, its clients first.
			if( auto client = UAClient::Find(FlowsSlug(), cred) )
				UAClient::RemoveClient( move(client) );
		}
		Flows().Stop();
		FlowServer::Shutdown( false );
		boost::asio::io_context io;
		optional<boost::asio::ip::tcp::acceptor> holder{ std::in_place, io, boost::asio::ip::tcp::endpoint{boost::asio::ip::address_v4::loopback(), port} };
		FlowServer::Start();
		auto [clock, onClock] = OnClock( Clock::now() );
		absl::Cleanup restore = [&]{//for the tests after, ahead of the host's own restart.
			holder.reset();
			if( !FlowServer::Instance() ){
				Flows().Stop();
				FlowServer::Start();
			}
		};
		EXPECT_FALSE( FlowServer::Instance() ) << "the port is another's";
		EXPECT_TRUE( WaitUntil([&]{ return Fault( flow ).Message.contains( "The flow server is not running" ); }) ) << "its fault says why:  " << Fault( flow ).Message;
		EXPECT_FALSE( Running(flow) );
		holder.reset();
		clock->Advance( FlowHost::RetryInterval );
		ASSERT_TRUE( WaitUntil([]{ return FlowServer::Instance()!=nullptr; }) ) << "the host's retry listens once the port is free";
		ASSERT_TRUE( WaitUntil([&]{ return Running( flow ); }) ) << "and the flow that waited for it starts:  " << Fault( flow ).Message;
		let reconnected = [&]( function<bool()> f ){//a client of the server before reconnects first.
			return WaitUntil( [&]{ try{ return f(); }catch( const std::exception& ){ return false; } }, 15s );
		};
		EXPECT_TRUE( reconnected([&]{ return Listed( slug ) && Listed( slug, "mean" ); }) ) << "its folder as its current version has it";
		Inject( flow, "in", jobject{{"payload", 2.5}} );
		EXPECT_TRUE( reconnected([&]{ return Served( slug, "mean" )==jvalue{ 2.5 }; }) );
	}
}