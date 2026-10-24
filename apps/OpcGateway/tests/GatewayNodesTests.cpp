//Servers phase 3a (#383, docs/servers/plan.md):  the endpoint's served address space - a group's members under Objects/<slug>/…
//at their paths on the source, in a namespace per connection and remote URI, with the attributes their rows hold, Historizing and
//an HA Configuration from their thresholds;  membership through HistHost's hooks;  and the group's rights on its nodes.
#include <open62541/client_highlevel.h>
#include <jde/tests/WaitUntil.h>
#include <jde/fwk/process/execution.h>
#include <jde/opc/tests/UATestClient.h>
#include <jde/opc/server/HAConfiguration.h>
#include <jde/opc/server/UAServer.h>
#include "../src/server/GatewayServer.h"
#include "../src/hist/HistRows.h"
#include "utils/HistGroupFixture.h"
#define let const auto

namespace Jde::Opc::Gateway::Tests{
	using Opc::Tests::UATestClient;
	constexpr UA_UInt16 _own{ 1 };//the endpoint's own namespace, the folders' (spec *Namespaces*).
	constexpr sv _ns0Uri{ "http://opcfoundation.org/UA/" };

	struct GatewayNodesTests : HistGroupFixture{
		α SetUp()->void override{
			HistGroupFixture::SetUp();
			if( !IsSkipped() && !GatewayServer::Instance() )
				GTEST_SKIP() << "The gateway serves no endpoint:  /gateway/server.";
		}
		Ω Ssl()ι->Crypto::CryptoSettings{ return Crypto::CryptoSettings{ Json::FindDefaultObject(Settings::AsObject("/http/gateway"), "ssl"), {} }; }
		Ω Url()ι->string{ return Ƒ( "opc.tcp://127.0.0.1:{}", Settings::FindNumber<PortType>("/gateway/server/port").value_or(4841) ); }
		Ω Issued()ι->UA_ExtensionObject{ return UATestClient::Issued( Ƒ("{:x}", AppClient()->SessionId()) ); }//this program's hub session:  the suite's user.
		Ω Client()ε->up<UATestClient>{
			auto y = mu<UATestClient>( Url(), Ssl(), Issued() );
			y->ConnectOrThrow();
			return y;
		}
		//The endpoint's index for a connection's remote URI, as its NamespaceArray has it;  none until a node of that namespace is served.
		Ω Mapped( sv remote, const ServerCnnctnNK& slug=OpcServerSlug )ι->optional<UA_UInt16>{
			let uri = GatewayServer::NamespaceUri( slug, remote );
			for( let& [index, ns] : GatewayServer::Instance()->Server()->Namespaces() ){
				if( ns==uri )
					return (UA_UInt16)index;
			}
			return nullopt;
		}
		//A pump member's served node by its path, Objects/<slug>/<pump>/<name>, each segment in the pumps' mapped namespace.
		Ω Served( UATestClient& client, sv pump, sv name, const ServerCnnctnNK& slug=OpcServerSlug, sv remote=PumpsUri )ε->optional<NodeId>{
			let ns = Mapped( remote, slug );
			return ns ? client.Translate( NodeId::ObjectsFolder(), {{_own, slug}, {*ns, string{pump}}, {*ns, string{name}}} ) : nullopt;
		}
		Ω Folder( UATestClient& client, sv pump, const ServerCnnctnNK& slug=OpcServerSlug )ε->optional<NodeId>{
			let ns = Mapped( PumpsUri, slug );
			return ns ? client.Translate( NodeId::ObjectsFolder(), {{_own, slug}, {*ns, string{pump}}} ) : nullopt;
		}
		Ω DisplayName( UATestClient& client, const NodeId& node )ε->string{
			UA_LocalizedText text; UA_LocalizedText_init( &text );
			UAε( UA_Client_readDisplayNameAttribute(client, node, &text) );
			string y{ ToString(text.text) };
			UA_LocalizedText_clear( &text );
			return y;
		}
		Ω DataType( UATestClient& client, const NodeId& node )ε->NodeId{
			UA_NodeId type; UA_NodeId_init( &type );
			UAε( UA_Client_readDataTypeAttribute(client, node, &type) );
			return NodeId{ move(type) };
		}
		Ω AccessLevel( UATestClient& client, const NodeId& node )ε->UA_Byte{
			UA_Byte y{};
			UAε( UA_Client_readAccessLevelAttribute(client, node, &y) );
			return y;
		}
		Ω UserAccessLevel( UATestClient& client, const NodeId& node )ε->UA_Byte{
			UA_Byte y{};
			UAε( UA_Client_readUserAccessLevelAttribute(client, node, &y) );
			return y;
		}
		Ω WriteValue( UATestClient& client, const NodeId& node, double value )ι->UA_StatusCode{
			UA_Variant v; UA_Variant_setScalar( &v, &value, &UA_TYPES[UA_TYPES_DOUBLE] );
			return UA_Client_writeValueAttribute( client, node, &v );
		}
		Ω Historizing( UATestClient& client, const NodeId& node )ε->bool{
			UA_Boolean y{};
			UAε( UA_Client_readHistorizingAttribute(client, node, &y) );
			return y;
		}
		Ω SamplingInterval( UATestClient& client, const NodeId& node )ε->double{
			UA_Double y{};
			UAε( UA_Client_readMinimumSamplingIntervalAttribute(client, node, &y) );
			return y;
		}
		//An HA Configuration property's number, or its `object`'s, through the client:  none when the object or the property isn't there.
		Ω Setting( UATestClient& client, const NodeId& variable, sv property, sv object={} )ε->optional<double>{
			vector<std::pair<UA_UInt16,string>> path{ {0, string{Opc::Server::HAConfiguration::BrowseName}} };
			if( object.size() )
				path.emplace_back( 0, string{object} );
			path.emplace_back( 0, string{property} );
			auto node = client.Translate( variable, path );
			if( !node )
				return nullopt;
			auto v = client.Read( *node );
			if( !v.hasValue || !v.IsScalar() )
				return nullopt;
			return v.value.type->typeKind==UA_DATATYPEKIND_BOOLEAN ? (double)v.Get<UA_Boolean>( 0 ) : v.AsNumber<double>();
		}
		//Until the collector has described the member - its path written - which Describe does once its item is made.
		Ω WaitPath( const sp<HistGroupHost>& host, uint32 index )ε->void{
			Stopwatch sw;
			while( !host->Attributes(index) || !host->Attributes(index)->Path )
				sw.CheckTimeout( 15s, 10ms );
		}
	};

	//A member at its path, Objects/<slug>/pumpManual/motorRpm, as the spec's *Address space* has it:  its source's id in the namespace
	//`urn:jde:<slug>:urn:jde:pumps`, the attributes its row holds - DisplayName, DataType, AccessLevel with HistoryRead -
	//Historizing, MinimumSamplingInterval from its MinTimeInterval, BaseDataVariableType, and an HA Configuration with its thresholds.
	TEST_F( GatewayNodesTests, APumpMemberIsServedAtItsPath ){
		const OneNodeGroup group{ "gatewayNodesPath", RpmManual, {{"minTimeInterval", 1000}, {"exceptionDeviation", 10}} };
		WaitPath( group.Host, group.Index );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pumpManual", "motorRpm" ); return node.has_value(); }) ) << "the member isn't served at its path";
		let ns = Mapped( PumpsUri );
		ASSERT_TRUE( ns );
		EXPECT_GE( *ns, 2 ) << "a namespace of the endpoint's own, after its applicationUri's";
		EXPECT_EQ( *node, NodeId(*ns, RpmManual) ) << "the source's identifier, mapped";
		EXPECT_EQ( DisplayName(*client, *node), "motorRpm" );
		EXPECT_EQ( DataType(*client, *node), NodeId(0, UA_NS0ID_DOUBLE) );
		let access = AccessLevel( *client, *node );
		EXPECT_TRUE( access & UA_ACCESSLEVELMASK_READ );
		EXPECT_TRUE( access & UA_ACCESSLEVELMASK_HISTORYREAD );
		EXPECT_TRUE( Historizing(*client, *node) );
		EXPECT_EQ( SamplingInterval(*client, *node), 1000.0 );
		EXPECT_EQ( Setting(*client, *node, "MinTimeInterval"), optional<double>{1000} );
		EXPECT_EQ( Setting(*client, *node, "ExceptionDeviation"), optional<double>{10} );
		EXPECT_EQ( Setting(*client, *node, "Stepped"), optional<double>{1} );
		const Opc::Hist::AggregateConfiguration aggregates;//Part 13's, the library's, not the zeros open62541 instantiates.
		EXPECT_EQ( Setting(*client, *node, "TreatUncertainAsBad", "AggregateConfiguration"), optional<double>{(double)aggregates.TreatUncertainAsBad} );
		EXPECT_EQ( Setting(*client, *node, "PercentDataBad", "AggregateConfiguration"), optional<double>{(double)aggregates.PercentDataBad} );
		EXPECT_EQ( Setting(*client, *node, "PercentDataGood", "AggregateConfiguration"), optional<double>{(double)aggregates.PercentDataGood} );
		EXPECT_EQ( Setting(*client, *node, "UseSlopedExtrapolation", "AggregateConfiguration"), optional<double>{(double)aggregates.UseSlopedExtrapolation} );
		auto children = client->Browse( *client->Translate(NodeId::ObjectsFolder(), {{_own, OpcServerSlug}}) );
		EXPECT_TRUE( std::ranges::any_of(children, [](let& c){ return c.BrowseName=="pumpManual"; }) ) << "the path folder under the connection's";
	}

	//The endpoint is read-only until Phase 5 forwards a write (plan milestone I):  a served node keeps the source's Write in its
	//AccessLevel, but no session's UserAccessLevel has it - not on the unenforced group, as it ships, nor with Update granted on an
	//enforced one - so a Value write is refused rather than stored as the served value.
	TEST_F( GatewayNodesTests, AServedValueRefusesWrites ){
		const OneNodeGroup group{ "gatewayNodesReadOnly", RpmManual };
		WaitPath( group.Host, group.Index );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pumpManual", "motorRpm" ); return node.has_value(); }) );
		EXPECT_TRUE( AccessLevel(*client, *node) & UA_ACCESSLEVELMASK_WRITE ) << "the source's, which the fixture writes";
		let refused = [&]( sv when ){
			let user = UserAccessLevel( *client, *node );
			EXPECT_TRUE( user & UA_ACCESSLEVELMASK_READ ) << when;
			EXPECT_FALSE( user & UA_ACCESSLEVELMASK_WRITE ) << when;
			EXPECT_EQ( WriteValue(*client, *node, 0.0), UA_STATUSCODE_BADUSERACCESSDENIED ) << when;
		};
		refused( "unenforced" );
		auto reopen = EnforcedHistGroups();
		refused( "enforced, everything granted" );
	}

	//A threshold change rewrites the node's MinimumSamplingInterval and its HA Configuration (plan Phase 3a).
	TEST_F( GatewayNodesTests, ThresholdsRewriteTheNode ){
		const OneNodeGroup group{ "gatewayNodesThresholds", Rpm3, {{"minTimeInterval", 500}} };
		WaitPath( group.Host, group.Index );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pump3", "motorRpm" ); return node.has_value(); }) );
		EXPECT_EQ( SamplingInterval(*client, *node), 500.0 );
		Mutate( "updateHistGroupNode( id:$id, minTimeInterval:$interval, maxTimeInterval:$max )", {{"id", group.Index}, {"interval", 2500}, {"max", 60000}} );
		EXPECT_TRUE( WaitUntil([&]{ return SamplingInterval(*client, *node)==2500.0; }) );
		EXPECT_EQ( Setting(*client, *node, "MinTimeInterval"), optional<double>{2500} );
		EXPECT_EQ( Setting(*client, *node, "MaxTimeInterval"), optional<double>{60000} );
	}

	//A describe that keeps the path rewrites the node in place, an attribute the source dropped back to AddNode's default - the
	//DisplayName to the browse name, no Description - and one that changes the shape re-adds it, array to scalar included, which
	//open62541 can't write in place.
	TEST_F( GatewayNodesTests, ADescribeRewritesTheNode ){
		const OneNodeGroup group{ "gatewayNodesDescribe", Rpm1 };
		WaitPath( group.Host, group.Index );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pump1", "motorRpm" ); return node.has_value(); }) );
		let described = [&]( auto&& change ){
			auto attributes = *group.Host->Attributes( group.Index );
			change( attributes );
			GatewayServer::Instance()->NodeDescribed( group.Id, group.Index, move(attributes) );
		};
		let description = [&]{
			UA_LocalizedText text; UA_LocalizedText_init( &text );
			UAε( UA_Client_readDescriptionAttribute(*client, *node, &text) );
			string y{ ToString(text.text) };
			UA_LocalizedText_clear( &text );
			return y;
		};
		let shape = [&]()->optional<std::pair<UA_Int32,vector<UA_UInt32>>> {
			UA_Int32 rank{}; size_t size{}; UA_UInt32* dimensions{};
			if( UA_Client_readValueRankAttribute(*client, *node, &rank) || UA_Client_readArrayDimensionsAttribute(*client, *node, &size, &dimensions) )
				return nullopt;//between the re-add's delete and its add.
			vector<UA_UInt32> y{ dimensions, dimensions+size };
			UA_Array_delete( dimensions, size, &UA_TYPES[UA_TYPES_UINT32] );
			return std::pair{ rank, y };
		};

		described( []( HistNodeAttributes& a ){ a.DisplayName = "Motor speed"; a.Description = "rpm"; } );
		ASSERT_TRUE( WaitUntil([&]{ return DisplayName(*client, *node)=="Motor speed" && description()=="rpm"; }) );
		described( []( HistNodeAttributes& a ){ a.DisplayName = nullopt; a.Description = nullopt; } );
		EXPECT_TRUE( WaitUntil([&]{ return DisplayName(*client, *node)=="motorRpm" && description().empty(); }) ) << "dropped:  the browse name, no description";

		described( []( HistNodeAttributes& a ){ a.ValueRank = 1; a.ArrayDimensions = vector<uint32>{ 5 }; } );
		EXPECT_TRUE( WaitUntil([&]{ return shape()==std::pair{ UA_VALUERANK_ONE_DIMENSION, vector<UA_UInt32>{5} }; }) ) << "scalar to array";
		described( []( HistNodeAttributes& a ){ a.ValueRank = UA_VALUERANK_SCALAR; a.ArrayDimensions = nullopt; } );
		EXPECT_TRUE( WaitUntil([&]{ return shape()==std::pair{ UA_VALUERANK_SCALAR, vector<UA_UInt32>{} }; }) ) << "and back";
		EXPECT_EQ( Served(*client, "pump1", "motorRpm"), node ) << "at its path still";
	}

	//NodeJoined serves the member as its host has it when the add runs, not as the hook's row had it:  a describe that lands first
	//finds no node to update, so the row's empty attributes would leave it under its connection folder by its id.
	TEST_F( GatewayNodesTests, AJoinServesTheHostsAttributes ){
		const OneNodeGroup group{ "gatewayNodesJoin", Rpm3 };
		WaitPath( group.Host, group.Index );
		auto client = Client();
		ASSERT_TRUE( WaitUntil([&]{ return Served( *client, "pump3", "motorRpm" ).has_value(); }) );
		auto& server = *GatewayServer::Instance();
		server.NodeLeft( group.Id, group.Index );
		ASSERT_TRUE( WaitUntil([&]{ return !Served( *client, "pump3", "motorRpm" ); }) );
		let members = group.Host->HistMembers();
		auto member = *std::ranges::find( members, group.Index, &HistMember::Index );
		member.Attributes = {};//the row's, before any describe.
		server.NodeJoined( *group.Host, move(member) );
		EXPECT_TRUE( WaitUntil([&]{ return Served( *client, "pump3", "motorRpm" ).has_value(); }) ) << "at its path, from the host";
	}

	//Every node on a served path is its source node's, by id in the mapped namespace (spec *Namespaces*):  pump4's folder is 5004 there.
	//A member under another - Rpm4's EURange property - hangs off a folder standing in for its parent, with the parent's id, until the
	//parent is served, then off the parent's variable by HasComponent, as on the source, and off a folder again once the parent goes.
	//Read from the server:  a client connected before a namespace was added maps it to an index of its own.
	TEST_F( GatewayNodesTests, AServedAncestorIsTheParent ){
		constexpr UA_UInt32 euRange4{ 7045 };
		let itemsBefore = Items();
		let groupId = CreateGroup( "gatewayNodesAncestor" );
		absl::Cleanup purge = [&]{ Purge( groupId, itemsBefore ); };
		auto& ua = *GatewayServer::Instance()->Server()->Ptr();
		optional<UA_UInt16> ns;
		let nodeClass = [&]( UA_UInt32 node )->optional<UA_NodeClass>{
			UA_NodeClass c;
			return ns && !UA_Server_readNodeClass( &ua, NodeId{*ns, node}, &c ) ? optional<UA_NodeClass>{ c } : nullopt;
		};
		//Whether `node` hangs off `parent` by `reference`, its one inverse hierarchical reference.
		let under = [&]( UA_UInt32 node, UA_UInt32 parent, UA_UInt32 reference ){
			if( !ns )
				return false;
			const NodeId id{ *ns, node };
			UA_BrowseDescription d; UA_BrowseDescription_init( &d );
			d.nodeId = id;//a view:  not cleared.
			d.browseDirection = UA_BROWSEDIRECTION_INVERSE;
			d.referenceTypeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_HIERARCHICALREFERENCES );
			d.includeSubtypes = true;
			d.resultMask = UA_BROWSERESULTMASK_REFERENCETYPEID;
			auto found = UA_Server_browse( &ua, 0, &d );
			let y = found.statusCode==UA_STATUSCODE_GOOD && found.referencesSize==1
				&& NodeId{found.references[0].nodeId.nodeId}==NodeId{*ns, parent} && NodeId{found.references[0].referenceTypeId}==NodeId{0, reference};
			UA_BrowseResult_clear( &found );
			return y;
		};

		AddNode( groupId, euRange4 );
		ASSERT_TRUE( WaitUntil([&]{ ns = Mapped( PumpsUri ); return under( euRange4, Rpm4, UA_NS0ID_ORGANIZES ); }) ) << "described:  under a folder standing in for Rpm4";
		EXPECT_EQ( nodeClass(Rpm4), UA_NODECLASS_OBJECT );
		EXPECT_TRUE( under(Rpm4, Pump4, UA_NS0ID_ORGANIZES) ) << "in pump4's folder, by its source id";
		EXPECT_EQ( nodeClass(Pump4), UA_NODECLASS_OBJECT );

		let rpm = AddNode( groupId, Rpm4 );
		EXPECT_TRUE( WaitUntil([&]{ return nodeClass(Rpm4)==UA_NODECLASS_VARIABLE && under(Rpm4, Pump4, UA_NS0ID_ORGANIZES) && under(euRange4, Rpm4, UA_NS0ID_HASCOMPONENT); }) ) << "Rpm4 served:  its variable is the parent";

		Mutate( "purgeHistGroupNode( id:$id )", {{"id", rpm}} );
		EXPECT_TRUE( WaitUntil([&]{ return nodeClass(Rpm4)==UA_NODECLASS_OBJECT && under(euRange4, Rpm4, UA_NS0ID_ORGANIZES); }) ) << "Rpm4 gone:  a folder stands in again";
	}

	//The endpoint's work runs on a thread of its own, not the executor's (servers-3a review #15):  with every executor thread held, a
	//node's removal still lands.  On the executor it waited behind them, and an ActivateSession holding the service lock while the hub's
	//session lookup waited for one could stall both.  A thread busy elsewhere leaves fewer to hold, which this can't tell from a pass.
	TEST_F( GatewayNodesTests, TheEndpointNeedsNoExecutorThread ){
		const OneNodeGroup group{ "gatewayNodesExecutor", Rpm3 };
		WaitPath( group.Host, group.Index );
		auto& ua = *GatewayServer::Instance()->Server()->Ptr();
		optional<UA_UInt16> ns;
		let served = [&]{ UA_NodeClass c; return ns && !UA_Server_readNodeClass( &ua, NodeId{*ns, Rpm3}, &c ); };
		ASSERT_TRUE( WaitUntil([&]{ ns = Mapped( PumpsUri ); return served(); }) );
		struct Hold final{ std::atomic<uint> Holding; std::atomic_flag Release; };
		auto hold = ms<Hold>();//the posted waits' own:  one still queued when this returns has it.
		let threads = Settings::FindNumber<uint>( "/workers/executor/threads" ).value_or( std::thread::hardware_concurrency() );
		for( uint i=0; i<threads; ++i )
			boost::asio::post( *Executor(), [hold]{ ++hold->Holding; hold->Release.wait( false ); } );
		absl::Cleanup release = [hold]{ hold->Release.test_and_set(); hold->Release.notify_all(); };
		EXPECT_TRUE( WaitUntil([&]{ return hold->Holding==threads; }, 2s) ) << "every executor thread held";
		GatewayServer::Instance()->NodeLeft( group.Id, group.Index );
		EXPECT_TRUE( WaitUntil([&]{ return !served(); }, 5s) ) << "the removal ran with no executor thread free";
	}

	//A member added comes, once described, at its path, and a purged one goes with the folder it leaves empty;  a deleted group's
	//nodes go and a restored group's come back.
	TEST_F( GatewayNodesTests, MembersComeAndGo ){
		let itemsBefore = Items();
		let groupId = CreateGroup( "gatewayNodesMembers" );
		absl::Cleanup purge = [&]{ Purge( groupId, itemsBefore ); };
		auto client = Client();
		EXPECT_FALSE( Served(*client, "pump1", "motorRpm") );
		let index = AddNode( groupId, Rpm1 );
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pump1", "motorRpm" ); return node.has_value(); }) ) << "the hook adds the node once its row exists and the source describes it";
		EXPECT_TRUE( Folder(*client, "pump1") );

		Mutate( "deleteHistGroup( id:$id )", {{"id", groupId}} );
		EXPECT_TRUE( WaitUntil([&]{ return !Served( *client, "pump1", "motorRpm" ); }) ) << "a deleted group's nodes go";
		EXPECT_FALSE( Folder(*client, "pump1") ) << "and the folder they leave empty";
		Mutate( "restoreHistGroup( id:$id )", {{"id", groupId}} );
		ASSERT_TRUE( WaitUntil([&]{ return Served( *client, "pump1", "motorRpm" ).has_value(); }) ) << "a restored group's come back";
		EXPECT_EQ( DisplayName(*client, *node), "motorRpm" ) << "from the rows, before the source describes it again";

		Mutate( "purgeHistGroupNode( id:$id )", {{"id", index}} );
		EXPECT_TRUE( WaitUntil([&]{ return !Served( *client, "pump1", "motorRpm" ); }) ) << "a purged member goes";
		EXPECT_FALSE( Folder(*client, "pump1") );
		EXPECT_TRUE( client->Translate(NodeId::ObjectsFolder(), {{_own, OpcServerSlug}}) ) << "the connection folder stays";
		EXPECT_EQ( client->Read(*node).status, UA_STATUSCODE_BADNODEIDUNKNOWN );
	}

	//A connection nothing can reach:  its members serve from their rows while the connection fails (spec *Address space*, open
	//question #7).  Undescribed, a member sits under the connection folder by its id;  with the columns a run with the server up left -
	//written here as the collector writes them, then the group deleted and restored, the load a restart makes - it is at its path with
	//the source's attributes.
	TEST_F( GatewayNodesTests, ADeadConnectionServesFromItsRows ){
		const string slug{ "gatewayNodesDead" };
		PurgeServerCnnctn( slug );
		let connection = GetConnection( slug, "opc.tcp://127.0.0.1:1", "" ).Id;
		absl::Cleanup purgeConnection = [=]{ PurgeServerCnnctn( connection ); };
		let groupId = Id( Mutate("createHistGroup( name:$name, slug:$slug, serverConnectionId:$connection ){ id }", {{"name", "Dead 3a"}, {"slug", slug}, {"connection", connection}}) );
		absl::Cleanup purgeGroup = [=]{ try{ Mutate( "purgeHistGroup( id:$id )", {{"id", groupId}} ); }catch( const std::exception& e ){ WARNT( ELogTags::Test, "purgeHistGroup({}):  {}", groupId, e.what() ); } };
		let index = Id( Mutate("createHistGroupNode( histGroupId:$g, node:$node ){ id }", {{"g", groupId}, {"node", serialize(Ex(Rpm2))}}) );//by URI:  spelled without the server.
		auto client = Client();
		ASSERT_TRUE( WaitUntil([&]{ return client->Translate(NodeId::ObjectsFolder(), {{_own, slug}}).has_value(); }) ) << "the connection's folder";
		optional<UA_UInt16> ns; optional<NodeId> undescribed;
		ASSERT_TRUE( WaitUntil([&]{ ns = Mapped( PumpsUri, slug ); undescribed = ns ? client->Translate( NodeId::ObjectsFolder(), {{_own, slug}, {*ns, Ƒ("i={}", Rpm2)}} ) : nullopt; return undescribed.has_value(); }) ) << "the namespace is added with the first node of it, under the connection folder by its id";
		const NodeId served{ *ns, Rpm2 };
		EXPECT_EQ( *undescribed, served );
		EXPECT_EQ( DataType(*client, served), NodeId(0, UA_NS0ID_BASEDATATYPE) ) << "until the source has said";

		HistNodeAttributes written;
		written.DisplayName = "Motor speed";
		written.Description = "Measured motor speed, rpm.";
		written.DataType = ExNodeId{ jvalue{jobject{{"nsu", _ns0Uri}, {"i", UA_NS0ID_DOUBLE}}} };
		written.ValueRank = -1;
		written.AccessLevel = 3;
		written.Path = vector<HistPathSegment>{ {string{PumpsUri}, "pump2"}, {string{PumpsUri}, "motorRpm"} };
		DS()->ExecuteSync( UpdateHistNodeAttributesSql(index, written) );
		Mutate( "deleteHistGroup( id:$id )", {{"id", groupId}} );
		EXPECT_TRUE( WaitUntil([&]{ return client->Read(served).status==UA_STATUSCODE_BADNODEIDUNKNOWN; }) );
		Mutate( "restoreHistGroup( id:$id )", {{"id", groupId}} );
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pump2", "motorRpm", slug ); return node.has_value(); }) ) << "at its path, from the row";
		EXPECT_EQ( *node, served );
		EXPECT_EQ( DisplayName(*client, *node), "Motor speed" );
		EXPECT_EQ( DataType(*client, *node), NodeId(0, UA_NS0ID_DOUBLE) );
		EXPECT_EQ( AccessLevel(*client, *node), 3|UA_ACCESSLEVELMASK_HISTORYREAD );
		EXPECT_FALSE( client->Translate(NodeId::ObjectsFolder(), {{_own, slug}, {*ns, Ƒ("i={}", Rpm2)}}) ) << "no longer under the connection folder";
	}

	//An add that fails part way leaves nothing behind:  a row whose ValueRank and ArrayDimensions open62541 refuses together fails at
	//the variable, after its path folder is made;  the folder goes with it and the node is unmapped, so with the row put right the next
	//load serves it.  The group's second member, served after it in the same pass, says the pass is done, read from the server:  a
	//client connected before the namespace was added maps its index to one of its own.
	TEST_F( GatewayNodesTests, AFailedAddLeavesNothing ){
		const string slug{ "gatewayNodesFailed" };
		PurgeServerCnnctn( slug );
		let connection = GetConnection( slug, "opc.tcp://127.0.0.1:1", "" ).Id;
		absl::Cleanup purgeConnection = [=]{ PurgeServerCnnctn( connection ); };
		let groupId = Id( Mutate("createHistGroup( name:$name, slug:$slug, serverConnectionId:$connection ){ id }", {{"name", "Failed 3a"}, {"slug", slug}, {"connection", connection}}) );
		absl::Cleanup purgeGroup = [=]{ try{ Mutate( "purgeHistGroup( id:$id )", {{"id", groupId}} ); }catch( const std::exception& e ){ WARNT( ELogTags::Test, "purgeHistGroup({}):  {}", groupId, e.what() ); } };
		let addNode = [&]( UA_UInt32 node ){ return Id( Mutate("createHistGroupNode( histGroupId:$g, node:$node ){ id }", {{"g", groupId}, {"node", serialize(Ex(node))}}) ); };
		let index = addNode( Rpm2 );
		addNode( Rpm3 );//the higher index, served after.
		optional<UA_UInt16> ns;
		let second = [&]{ UA_NodeClass c; return ns && !UA_Server_readNodeClass( GatewayServer::Instance()->Server()->Ptr(), NodeId{*ns, Rpm3}, &c ); };
		ASSERT_TRUE( WaitUntil([&]{ ns = Mapped( PumpsUri, slug ); return second(); }) );
		auto client = Client();
		let reload = [&]{
			Mutate( "deleteHistGroup( id:$id )", {{"id", groupId}} );
			EXPECT_TRUE( WaitUntil([&]{ return !second(); }) );
			Mutate( "restoreHistGroup( id:$id )", {{"id", groupId}} );
			return WaitUntil( second );
		};
		const NodeId served{ *ns, Rpm2 };

		HistNodeAttributes written;
		written.DataType = ExNodeId{ jvalue{jobject{{"nsu", _ns0Uri}, {"i", UA_NS0ID_DOUBLE}}} };
		written.ValueRank = UA_VALUERANK_SCALAR;
		written.ArrayDimensions = vector<uint32>{ 3 };
		written.Path = vector<HistPathSegment>{ {string{PumpsUri}, "pump2"}, {string{PumpsUri}, "motorRpm"} };
		DS()->ExecuteSync( UpdateHistNodeAttributesSql(index, written) );
		ASSERT_TRUE( reload() );
		EXPECT_EQ( client->Read(served).status, UA_STATUSCODE_BADNODEIDUNKNOWN ) << "the variable refused";
		EXPECT_FALSE( Folder(*client, "pump2", slug) ) << "and the folder made for it gone";

		written.ArrayDimensions = nullopt;
		DS()->ExecuteSync( UpdateHistNodeAttributesSql(index, written) );
		ASSERT_TRUE( reload() );
		let node = Served( *client, "pump2", "motorRpm", slug );
		ASSERT_TRUE( node ) << "unmapped, so the next load serves it";
		EXPECT_EQ( *node, served );
	}

	//A session's rights on a served node are its group's access row (spec *Sessions and rights*):  the group's own, keyed by its
	//guid, else the table's.  Under histGroups enforced with the suite's user granted everything, the node is there;  with a row of
	//the group's own granting the user no Read, it isn't browsable - no path translates to it, its attributes and value are refused,
	//though its folder still lists the name, which is where open62541 gates (GatewayNodeAccess.h), and so are its HA Configuration's
	//properties, the group's thresholds - while the folders still are;  the row gone, the table's grant has it back.
	TEST_F( GatewayNodesTests, AUserWithoutReadSeesNoNode ){
		const OneNodeGroup group{ "gatewayNodesRights", Rpm4 };
		WaitPath( group.Host, group.Index );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pump4", "motorRpm" ); return node.has_value(); }) );
		let folder = Folder( *client, "pump4" );
		ASSERT_TRUE( folder );
		using Opc::Server::HAConfiguration::Child;
		auto& ua = *GatewayServer::Instance()->Server()->Ptr();
		let configuration = Child( ua, NodeId{*Mapped(PumpsUri), Rpm4}, Opc::Server::HAConfiguration::BrowseName );
		ASSERT_TRUE( configuration );
		let interval = Child( ua, *configuration, "MinTimeInterval" );
		let aggregates = Child( ua, *configuration, "AggregateConfiguration" );
		let bad = aggregates ? Child( ua, *aggregates, "PercentDataBad" ) : nullopt;
		ASSERT_TRUE( interval && bad );
		let displayName = [&]{ UA_LocalizedText text; UA_LocalizedText_init( &text ); let sc = UA_Client_readDisplayNameAttribute( *client, *node, &text ); UA_LocalizedText_clear( &text ); return sc; };
		auto reopen = EnforcedHistGroups();
		EXPECT_TRUE( WaitUntil([&]{ return Served( *client, "pump4", "motorRpm" ).has_value(); }) ) << "the table's grant to the suite's user";
		EXPECT_EQ( client->Read(*node).status, UA_STATUSCODE_GOOD );
		ScopeHistGroup( group.Id, Access::ERights::None );
		absl::Cleanup unscope = [&]{ try{ UnscopeHistGroup( group.Id ); }catch( const std::exception& e ){ ADD_FAILURE() << "the group's resource wasn't deleted:  " << e.what(); } };
		EXPECT_TRUE( WaitUntil([&]{ return !Served( *client, "pump4", "motorRpm" ); }) ) << "the group's own row grants no Read:  no path leads to the node";
		EXPECT_EQ( displayName(), UA_STATUSCODE_BADUSERACCESSDENIED );
		EXPECT_EQ( client->Read(*node).status, UA_STATUSCODE_BADUSERACCESSDENIED );
		EXPECT_THROW( client->Browse(*node), UAException );
		EXPECT_EQ( client->Read(*interval).status, UA_STATUSCODE_BADUSERACCESSDENIED ) << "its HA Configuration's properties are the group's";
		EXPECT_EQ( client->Read(*bad).status, UA_STATUSCODE_BADUSERACCESSDENIED ) << "and AggregateConfiguration's";
		EXPECT_TRUE( Folder(*client, "pump4") ) << "the folders stay open";
		EXPECT_TRUE( client->Translate(NodeId::ObjectsFolder(), {{_own, OpcServerSlug}, {_own, "_Connection"}}) ) << "and _Connection";
		std::move( unscope ).Invoke();
		EXPECT_TRUE( WaitUntil([&]{ return Served( *client, "pump4", "motorRpm" ).has_value(); }) ) << "back to the table's row";
		EXPECT_EQ( displayName(), UA_STATUSCODE_GOOD );
		EXPECT_EQ( client->Read(*node).status, UA_STATUSCODE_GOOD );
		EXPECT_EQ( client->Read(*interval).status, UA_STATUSCODE_GOOD );
	}
}