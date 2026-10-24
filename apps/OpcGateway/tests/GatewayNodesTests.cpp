//Servers phase 3a (#383, docs/servers/plan.md):  the endpoint's served address space - a group's members under Objects/<slug>/…
//at their paths on the source, in a namespace per connection and remote URI, with the attributes their rows hold, Historizing and
//an HA Configuration from their thresholds;  membership through HistHost's hooks;  and the group's rights on its nodes.
#include <open62541/client_highlevel.h>
#include <jde/tests/WaitUntil.h>
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
		//An HA Configuration property's number, through the client:  none when the object or the property isn't there.
		Ω Setting( UATestClient& client, const NodeId& variable, sv property )ε->optional<double>{
			auto node = client.Translate( variable, {{0, string{Opc::Server::HAConfiguration::BrowseName}}, {0, string{property}}} );
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
		auto children = client->Browse( *client->Translate(NodeId::ObjectsFolder(), {{_own, OpcServerSlug}}) );
		EXPECT_TRUE( std::ranges::any_of(children, [](let& c){ return c.BrowseName=="pumpManual"; }) ) << "the path folder under the connection's";
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

	//A session's rights on a served node are its group's access row (spec *Sessions and rights*):  the group's own, keyed by its
	//guid, else the table's.  Under histGroups enforced with the suite's user granted everything, the node is there;  with a row of
	//the group's own granting the user no Read, it isn't browsable - no path translates to it, its attributes and value are refused,
	//though its folder still lists the name, which is where open62541 gates (GatewayNodeAccess.h) - while the folders still are;  the
	//row gone, the table's grant has it back.
	TEST_F( GatewayNodesTests, AUserWithoutReadSeesNoNode ){
		const OneNodeGroup group{ "gatewayNodesRights", Rpm4 };
		WaitPath( group.Host, group.Index );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pump4", "motorRpm" ); return node.has_value(); }) );
		let folder = Folder( *client, "pump4" );
		ASSERT_TRUE( folder );
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
		EXPECT_TRUE( Folder(*client, "pump4") ) << "the folders stay open";
		EXPECT_TRUE( client->Translate(NodeId::ObjectsFolder(), {{_own, OpcServerSlug}, {_own, "_Connection"}}) ) << "and _Connection";
		std::move( unscope ).Invoke();
		EXPECT_TRUE( WaitUntil([&]{ return Served( *client, "pump4", "motorRpm" ).has_value(); }) ) << "back to the table's row";
		EXPECT_EQ( displayName(), UA_STATUSCODE_GOOD );
		EXPECT_EQ( client->Read(*node).status, UA_STATUSCODE_GOOD );
	}
}