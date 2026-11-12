#include <boost/asio/ip/tcp.hpp>
#include <jde/fwk/process/execution.h>
#include <jde/tests/Capture.h>
#include <jde/opc/ServerTrust.h>
#include <jde/opc/openProtocol/logTags.h>
#include "../src/auth/OpcServerSession.h"
#include "utils/ClientFixture.h"
#include "utils/DriverRows.h"
#include "utils/HistGroupFixture.h"
#include "utils/Subscribing.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	using OpenProtocol::Controller;

	//docs/open-protocol/plan.md Phase 4 (#466):  a driver row's own UA server (drivers/DriverHost.h) and the Open Protocol driver's
	//skeleton behind it, `_Op` over a session with the library's test controller (drivers/openProtocol/OpenProtocolDriver.h) -
	//through the gateway's clients, as a web session, a group's collector and the socket reach any server.
	struct DriverHostTests : HistGroupFixture{
	protected:
		//A row for the suite's controller, made while the gateway runs, with its server listening:  purged as it goes.
		struct Row final{
			Row( string slug, sv driver={} )ε:Slug{ move(slug) }{
				PurgeServerCnnctn( Slug );
				let created = CreateDriverRow( Slug, driver );
				THROW_IF( !created, "{}", created.error() );
				Id = *created;
				if( !WaitHost(Slug) ){
					PurgeServerCnnctn( Id );
					THROW( "the server of '{}' never started.", Slug );
				}
			}
			Row( const Row& )=delete;
			~Row(){
				PurgeServerCnnctn( Id );
				WaitNoHost( Slug );
			}
			string Slug;
			ServerCnnctnPK Id;
		};
		//A PF6000 station that answers every revision the codec has, and numbers its messages.
		Ω Station()ι->Controller::Script{
			return { .Identity={ .CellId=1, .ChannelId=2, .ControllerName="Line 3 station 7", .SupplierCode="ACT", .OpenProtocolVersion="2.20.1", .ControllerSoftwareVersion="3.11.5", .ToolSoftwareVersion="1.4.2",
				.RbuType="Gold", .ControllerSerialNumber="C00123456", .SystemType=3, .SystemSubtype=1, .SequenceNumberSupport=1, .LinkingSupport=0, .StationId=7, .StationName="Station 7", .ClientId=1 } };
		}
		Ω Op( const Row& row, sv name )ε->Value{ return ReadOp( row.Slug, name, SessionCredential() ); }//as a web session's client reads it.
		//Until `_Op`'s `name` is as `is` has it;  a read the row's server is not there for is one that is not yet.
		Ω WaitOp( const Row& row, sv name, function<bool(const Value&)> is, Duration timeout=15s )ι->bool{
			return WaitUntil( [&]{
				try{
					return is( Op(row, name) );
				}
				catch( const std::exception& ){
					return false;
				}
			}, timeout );
		}
		Ω WaitConnected( const Row& row, bool connected=true )ι->bool{ return WaitOp( row, "Connected", [=]( let& v ){ return OpFlag( v )==connected; } ); }
		Ω Collector( const Row& row )ι->sp<UAClient>{ return UAClient::Find( row.Slug, HistHost::Instance().Collector(row.Slug) ); }
		//A group over the row goes, and its collector's items before the row does:  an item's delete comes _itemDeleteDelay on, and
		//would find its client closed.
		Ω PurgeGroup( uint32 group, const Row& row )ι->void{
			try{
				Mutate( "purgeHistGroup( id:$id )", {{"id", group}} );
				let dropped = WaitUntil( [&]{ let collector = Collector( row ); return !collector || !collector->TryMonitoredNodes() || !collector->TryMonitoredNodes()->Count(); } );
				EXPECT_TRUE( dropped ) << "the collector's items outlived group " << group;
			}
			catch( const std::exception& e ){
				ADD_FAILURE() << "purgeHistGroup(" << group << "): " << e.what();
			}
		}
		Ω Delete( const Row& row )ε->void{ QL().QuerySync<jvalue>( Ƒ("mutation deleteServerConnection( id:{} )", row.Id), {}, {UserPK::System} ); }
		Ω Restore( const Row& row )ε->void{ QL().QuerySync<jvalue>( Ƒ("mutation restoreServerConnection( id:{} )", row.Id), {}, {UserPK::System} ); }
		//The sessions the row's server holds of a token type, by UAAccess's label.
		Ω Sessions( const Row& row, sv type )ι->vector<Opc::Server::UAAccess::Session>{
			vector<Opc::Server::UAAccess::Session> y;
			if( let host = DriverHost::Find(row.Slug); host ){
				for( auto& session : host->Sessions() ){
					if( session.Type==type )
						y.push_back( move(session) );
				}
			}
			return y;
		}
	};

	//A row inserted while the gateway runs has its server with no restart:  `_Op` browsed under the row's one namespace and read
	//through the socket, a web session's issued token, the row's first.
	TEST_F( DriverHostTests, ARowsServerServesOp ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const Row row{ "driver-op" };
		let host = DriverHost::Find( row.Slug );
		ASSERT_TRUE( host );
		EXPECT_EQ( host->Port(), ServerCnnctn::DriverPortBase()+row.Id );
		EXPECT_EQ( ServerCnnctn::DriverPortBase(), 4980u ) << "the suite's, clear of the shipped 4870";

		let op = Socket().QuerySync( "node( opc: $opc, path: $path ){ id name nodeClass }", {{"opc", row.Slug}, {"path", "_Op"}} );//a bare segment is the row's namespace's.
		TRACE( "_Op: {}", serialize(op) );
		const NodeId opId{ ExNodeId{op}.nodeId };
		EXPECT_EQ( opId.ToString(), NodeId{UA_NODEID_STRING_ALLOC(ServerCnnctn::DriverBrowseNs, "_Op")}.ToString() );
		EXPECT_EQ( Json::FindNumber<uint>(op.as_object(), "nodeClass"), UA_NODECLASS_OBJECT );
		let children = Socket().QuerySync( "node( opc: $opc, id: $id ){ children{ name nodeClass } }", {{"opc", row.Slug}, {"id", opId.ToJson()}} ).as_object();
		vector<string> names;
		for( let& child : Json::AsArray(children, "children") ){
			names.push_back( Json::AsString(child.as_object(), "name") );
			EXPECT_EQ( Json::FindNumber<uint>(child.as_object(), "nodeClass"), UA_NODECLASS_VARIABLE ) << names.back();
		}
		std::ranges::sort( names );
		EXPECT_EQ( names, (vector<string>{"Connected", "Identity", "LastError", "LinkLevel", "LostResults", "Reconnects", "Results", "Revisions", "Since", "Skew", "Url"}) );
		let read = [&]( sv name ){ return Socket().QuerySync( "node( opc: $opc, id: $id ){ value }", {{"opc", row.Slug}, {"id", OpNode(name).ToJson()}} ).as_object().at( "value" ); };
		EXPECT_EQ( read("Url"), jvalue{ControllerUrl()} );
		ASSERT_TRUE( WaitUntil([&]{ return read( "Connected" )==jvalue{ true }; }) ) << "the session with the controller never came up";
		let web = Sessions( row, "IssuedToken" );
		ASSERT_EQ( web.size(), 1u ) << "the socket's session, by issued token";
		EXPECT_EQ( web.front().User, AppClient()->UserPK() ) << "the socket is the app client's session";
		EXPECT_EQ( web.front().Mode, UA_MESSAGESECURITYMODE_SIGNANDENCRYPT );
		EXPECT_TRUE( web.front().PolicyUri.ends_with("#Basic256Sha256") ) << web.front().PolicyUri;

		//The row's one namespace, after UA's and the server's own.
		let namespaces = ReadValue( BlockAwait(UAClient::GetClient(row.Slug, SessionCredential())), NodeId{0, UA_NS0ID_SERVER_NAMESPACEARRAY} );
		ASSERT_EQ( namespaces.value.arrayLength, 3u );
		EXPECT_EQ( ToString(namespaces.Get<UA_String>(2)), "urn:jde:driver-op:open-protocol" );

		//What the session reported, and the clock read at the connect.
		ASSERT_TRUE( WaitOp(row, "Revisions", []( let& v ){ return OpText( v )=="0001:6 0080:1"; }) ) << OpText( Op(row, "Revisions") );
		EXPECT_EQ( OpFlag(Op(row, "LinkLevel")), true ) << "MID 0002 at 6 says the controller numbers its messages";
		EXPECT_EQ( OpText(Op(row, "LastError")), "" );
		EXPECT_EQ( OpCount(Op(row, "Reconnects")), 0u ) << "a first connection is no reconnect";
		EXPECT_EQ( OpCount(Op(row, "Results")), 0u );
		EXPECT_EQ( OpCount(Op(row, "LostResults")), 0u );
		let identity = Json::Parse( OpText(Op(row, "Identity")) );
		EXPECT_EQ( Json::FindDefaultSV(identity, "controllerName"), "Line 3 station 7" ) << serialize( identity );
		EXPECT_EQ( Json::FindDefaultSV(identity, "controllerSerialNumber"), "C00123456" );
		EXPECT_EQ( Json::FindNumber<uint>(identity, "systemType"), optional<uint>{3} );
		EXPECT_EQ( Json::FindNumber<uint>(identity, "stationId"), optional<uint>{7} );
		let skew = Op( row, "Skew" );
		ASSERT_TRUE( !skew.status && UA_Variant_hasScalarType(&skew.value, &UA_TYPES[UA_TYPES_DOUBLE]) ) << UAException::Message( skew.status );
		EXPECT_LT( std::abs(skew.Get<UA_Double>(0)), 5.0 ) << "the controller keeps the gateway's own clock and zone";
		let since = Op( row, "Since" );
		ASSERT_TRUE( since.hasValue && UA_Variant_hasScalarType(&since.value, &UA_TYPES[UA_TYPES_DATETIME]) );
		EXPECT_LT( Clock::now()-UADateTime{since.Get<UA_DateTime>(0)}.Time(), Duration{1min} );

		//Nothing is writable:  each variable is the driver's to write.
		let attributes = Socket().QuerySync( "node( opc: $opc, id: $id ){ accessLevel userAccessLevel }", {{"opc", row.Slug}, {"id", OpNode("Reconnects").ToJson()}} ).as_object();
		EXPECT_EQ( Json::FindNumber<uint>(attributes, "accessLevel"), optional<uint>{UA_ACCESSLEVELMASK_READ} ) << serialize( attributes );
		EXPECT_EQ( Json::FindNumber<uint>(attributes, "userAccessLevel"), optional<uint>{UA_ACCESSLEVELMASK_READ} ) << serialize( attributes );
	}

	//`_Op` follows the session:  no controller, each failed attempt in LastError;  the link up, with the identity and the settled
	//revisions;  the controller stopped, Connected false and why;  and back, a reconnect, on what the controller now answers.
	TEST_F( DriverHostTests, OpFollowsTheLink ){
		const Row row{ "driver-link" };
		ASSERT_TRUE( WaitOp(row, "LastError", []( let& v ){ return OpText( v ).size()>0; }) ) << "an attempt on a controller that is not there";
		TRACE( "no controller: {}", OpText(Op(row, "LastError")) );
		EXPECT_EQ( OpFlag(Op(row, "Connected")), false );
		EXPECT_EQ( OpText(Op(row, "Identity")), "" );
		EXPECT_EQ( OpText(Op(row, "Revisions")), "" );
		EXPECT_EQ( Op(row, "Skew").status, UA_STATUSCODE_BADWAITINGFORINITIALDATA ) << "no clock read yet";
		{
			Controller station{ *Executor(), ControllerPort(), Station() };
			ASSERT_TRUE( WaitConnected(row) );
			ASSERT_TRUE( WaitOp(row, "Revisions", []( let& v ){ return OpText( v )=="0001:6 0080:1"; }) ) << OpText( Op(row, "Revisions") );
			EXPECT_EQ( OpCount(Op(row, "Reconnects")), 0u );
			EXPECT_EQ( OpFlag(Op(row, "LinkLevel")), true );
		}
		ASSERT_TRUE( WaitConnected(row, false) ) << "the controller stopped";
		let lost = OpText( Op(row, "LastError") );
		EXPECT_FALSE( lost.empty() );
		TRACE( "controller stopped: {}", lost );
		EXPECT_EQ( OpCount(Op(row, "Reconnects")), 0u ) << "counted when the link is back";
		EXPECT_TRUE( OpText(Op(row, "Identity")).contains("Line 3 station 7") ) << "the last connection's, kept through the loss";

		auto older = Station();//a controller that has MID 0001 at 3 and no higher:  no serial number, and no sequence numbers.
		older.Revisions[1] = 3;
		older.Identity.ControllerName = "Line 3 station 8";
		Controller station{ *Executor(), ControllerPort(), move(older) };
		ASSERT_TRUE( WaitConnected(row) ) << "the controller restarted";
		EXPECT_EQ( OpCount(Op(row, "Reconnects")), 1u );
		ASSERT_TRUE( WaitOp(row, "Revisions", []( let& v ){ return OpText( v )=="0001:3 0080:1"; }) ) << OpText( Op(row, "Revisions") );
		EXPECT_EQ( OpFlag(Op(row, "LinkLevel")), false );
		let identity = Json::Parse( OpText(Op(row, "Identity")) );
		EXPECT_EQ( Json::FindDefaultSV(identity, "controllerName"), "Line 3 station 8" ) << serialize( identity );
		EXPECT_FALSE( identity.contains("controllerSerialNumber") ) << "revision 4's: " << serialize( identity );
		EXPECT_EQ( RevisionsOf(station, 1), (vector<uint16_t>{6, 5, 4, 3}) ) << "the step-down";
	}

	//The controller's clock, read by MID 0080 at each connect in the row's zone:  shown in Skew, warned of past maxSkew, and
	//Bad_NotSupported where the controller has no MID 0080.
	TEST_F( DriverHostTests, ASkewPastMaxSkewWarns ){
		auto ahead = Station();
		ahead.Zone = std::chrono::locate_zone( "Europe/Stockholm" );
		ahead.Skew = 10min;
		Logging::Capture warnings{ OpenProtocol::LogTag, ELogLevel::Warning };
		let warned = [&]{ return warnings.Find( []( const Logging::Entry& e ){ return e.Level==ELogLevel::Warning && e.Message().contains( "maxSkew" ); } ); };
		Controller station{ *Executor(), ControllerPort(), move(ahead) };
		const Row row{ "driver-skew", R"({"timeZone":"Europe/Stockholm"})" };
		let skewNear = [&]( double seconds ){ return WaitOp( row, "Skew", [=]( const Value& v ){ return !v.status && UA_Variant_hasScalarType( &v.value, &UA_TYPES[UA_TYPES_DOUBLE] ) && std::abs( v.Get<UA_Double>(0)-seconds )<5; } ); };
		ASSERT_TRUE( skewNear(600) ) << serialize( Op(row, "Skew").DataValueJson() );
		ASSERT_EQ( warned().size(), 1u );
		TRACE( "{}", warned().front().Message() );
		EXPECT_TRUE( warned().front().Message().contains("ahead of the gateway's") ) << warned().front().Message();
		EXPECT_TRUE( warned().front().Message().contains(row.Slug) ) << warned().front().Message();

		//A clock inside maxSkew, at the next connect:  shown, and no warning.
		warnings.Restart();
		station.Edit( []( Controller::Script& script ){ script.Skew = -2min; } );
		station.Drop();
		ASSERT_TRUE( WaitOp(row, "Reconnects", []( let& v ){ return OpCount( v )==1u; }) );
		ASSERT_TRUE( skewNear(-120) ) << serialize( Op(row, "Skew").DataValueJson() );
		EXPECT_TRUE( warned().empty() );

		//A controller that has no MID 0080, as the conformance simulator:  no skew to show, and the refusal in LastError.
		station.Edit( []( Controller::Script& script ){ script.Refuses[80] = 99; } );
		station.Drop();
		ASSERT_TRUE( WaitOp(row, "Skew", []( let& v ){ return v.status==UA_STATUSCODE_BADNOTSUPPORTED; }) ) << serialize( Op(row, "Skew").DataValueJson() );
		EXPECT_TRUE( OpText(Op(row, "LastError")).contains("MID 0080") ) << OpText( Op(row, "LastError") );
		EXPECT_EQ( OpFlag(Op(row, "Connected")), true ) << "the link stands without the clock";
	}

	//A group over `_Op.Reconnects` collects:  the collector presents the process's own pair, for its channel and as its token, and
	//the row's server opens its session as the gateway's own user.
	TEST_F( DriverHostTests, AGroupCollectsAsTheGatewaysUser ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const Row row{ "driver-group" };
		let group = Id( Mutate("createHistGroup( name:$name, slug:$slug, serverConnectionId:$connection ){ id }", {{"name", "Driver reconnects"}, {"slug", "driver-reconnects"}, {"connection", row.Id}}) );
		absl::Cleanup purge = [&]{ PurgeGroup( group, row ); };
		let member = AddNode( group, jvalue{jobject{ {"nsu", DriverHost::NamespaceUri(row.Slug, ServerCnnctn::KindOpenProtocol)}, {"s", "_Op.Reconnects"} }} );
		{
			let host = Host( group );
			ASSERT_NO_THROW( WaitSubscribed(host, 1) );
			ASSERT_NO_THROW( WaitStored(host, member, Joined, 1) );
			let collectors = Sessions( row, "Certificate" );
			ASSERT_EQ( collectors.size(), 1u ) << "the collector's session, by the process's certificate";
			EXPECT_TRUE( AppClient()->UserPK() );
			EXPECT_EQ( collectors.front().User, AppClient()->UserPK() ) << "the gateway's own user";

			//The link lost and back is the member's next value.
			ASSERT_TRUE( WaitConnected(row) );
			station.Drop();
			vector<Opc::Hist::ReadValue> stored;
			ASSERT_NO_THROW( stored = WaitStored(host, member, Joined, 2) );
			EXPECT_EQ( Hist::ToUA(stored.back().Value).Get<UA_UInt32>(0), 1u );
		}
	}

	//A new row's first web session is admitted:  the host issued the slug's certificate before it built its trust list.  And one
	//after the certificate's file is deleted and issued again at the next connect:  the list re-reads the file on the failed verify.
	TEST_F( DriverHostTests, AWebSessionIsAdmittedAfterItsCertificateIsIssuedAgain ){
		const Row row{ "driver-web" };
		let certificate = UAClient::CryptoSettings( row.Slug ).Certificate.Path;
		ASSERT_TRUE( fs::exists(certificate) ) << certificate << ":  no web user has opened the row";
		let first = Crypto::ReadCertificate( certificate );
		ASSERT_NO_THROW( Socket().QuerySync("node( opc: $opc, id: $id ){ value }", {{"opc", row.Slug}, {"id", OpNode("Url").ToJson()}}) );
		EXPECT_EQ( Sessions(row, "IssuedToken").size(), 1u );

		fs::remove( certificate );
		let cred = SessionCredential();//the session's jwt, a client of its own:  a channel made now, which presents the new certificate.
		auto client = TryConnect( row.Slug, cred );
		ASSERT_TRUE( client ) << client.error()->what();
		ASSERT_TRUE( fs::exists(certificate) );
		EXPECT_NE( Crypto::ReadCertificate(certificate), first ) << "the connect issued no new certificate";
		EXPECT_EQ( OpText(ReadValue(*client, OpNode("Url"))), ControllerUrl() );
		EXPECT_EQ( Sessions(row, "IssuedToken").size(), 2u );
	}

	//The row's clients trust the row's server by the process's own certificate, anchored beside /gateway/trustedCertDirs:  with no
	//directory at all the row's client connects, and a UA row dialing the same server, which has no anchor, refuses its certificate.
	//The server in turn admits the row's slug and the process's pair alone.
	TEST_F( DriverHostTests, TheServersCertificateIsRefusedWithoutTheAnchor ){
		const Row row{ "driver-anchor" };
		let url = RowOf( row.Id ).DriverUrl();
		const string stranger{ "driver-no-anchor" };
		PurgeServerCnnctn( stranger );
		let uri = AppClient()->SslSettings->Certificate.SanUri();
		let strangerId = GetConnection( stranger, url, uri ).Id;
		absl::Cleanup purge = [strangerId]{ PurgeServerCnnctn( strangerId ); };
		let cred = SessionCredential();
		{
			ServerTrust::OverrideTrustedCertDirs( vector<fs::path>{} );//the suite's own list holds the process's certificate, and would hide the anchor.
			absl::Cleanup restore = []{ ServerTrust::OverrideTrustedCertDirs( nullopt ); };
			auto own = TryConnect( row.Slug, cred );
			ASSERT_TRUE( own ) << own.error()->what();
			EXPECT_EQ( OpText(ReadValue(*own, OpNode("Url"))), ControllerUrl() );
			UAClient::RemoveClient( move(*own) );

			let refused = TryConnect( stranger, cred );
			ASSERT_FALSE( refused ) << "a client with no anchor trusted the driver server";
			const string what{ refused.error()->what() };
			TRACE( "no anchor: {}", what );
			EXPECT_TRUE( what.contains(Ƒ("server certificate for '{}' rejected", url)) ) << what;
		}
		//With the suite's list back the stranger trusts the server, which refuses the stranger:  its slug's certificate is neither of the two.
		let refused = TryConnect( stranger, cred );
		ASSERT_FALSE( refused ) << "the driver server admitted another slug's certificate";
		TRACE( "not of the two: {}", refused.error()->what() );
		EXPECT_FALSE( string{refused.error()->what()}.contains("server certificate for") ) << refused.error()->what();
		EXPECT_TRUE( WaitUntil([&]{ return Sessions( row, "IssuedToken" ).empty(); }) ) << "the stranger's session";
	}

	//A host follows its row:  a delete stops it, the controller hearing MID 0003 and the close;  a restore starts it and the
	//link comes back;  an edit of `driver` starts it again on the row as it now is, and a client that was subscribed reconnects.
	TEST_F( DriverHostTests, AHostFollowsItsRow ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const Row row{ "driver-lifecycle" };
		ASSERT_TRUE( WaitUntil([&]{ return station.Clients()==1; }) );
		ASSERT_TRUE( WaitConnected(row) );

		ASSERT_NO_THROW( Delete(row) );
		ASSERT_TRUE( WaitNoHost(row.Slug) );//gone once stopped.
		EXPECT_EQ( RevisionsOf(station, 3).size(), 1u ) << "MID 0003 before the close";
		EXPECT_TRUE( WaitUntil([&]{ return station.Clients()==0; }) );
		EXPECT_FALSE( TryConnect(row.Slug, SessionCredential()) ) << "a deleted row's server is still there";

		ASSERT_NO_THROW( Restore(row) );
		ASSERT_TRUE( WaitHost(row.Slug) );
		EXPECT_TRUE( WaitUntil([&]{ return station.Clients()==1; }) );
		ASSERT_TRUE( WaitConnected(row) );

		let cred = SessionCredential();
		auto client = TryConnect( row.Slug, cred );
		ASSERT_TRUE( client ) << client.error()->what();
		auto listener = ms<PushCount>();
		absl::Cleanup unsubscribe = [&]{//the item's delete comes _itemDeleteDelay on:  before the row's purge closes its client.
			UAClient::Unsubscribe( listener );
			WaitUntil( [&]{ return std::ranges::none_of( UAClient::LiveClients(), [&]( let& c ){ return c->Slug()==row.Slug && c->TryMonitoredNodes() && c->TryMonitoredNodes()->Count(); } ); } );
		};
		ASSERT_NO_THROW( SubscribeCounting(*client, listener, {OpNode("Revisions")}) );
		let lost = ( *client )->Handle();
		let heard = station.Frames().size();
		let pushes = listener->Pushes.load();
		let error = UpdateRow( row.Id, "driver:$driver", {{"driver", R"({"revisions":{"0001":2}})"}} );
		ASSERT_FALSE( error ) << *error;
		ASSERT_TRUE( WaitUntil([&]{ return RevisionsOf( station, 1, heard )==vector<uint16_t>{ 2 }; }) ) << "the session under the row's new ceiling";
		EXPECT_EQ( RevisionsOf(station, 3, heard).size(), 1u ) << "the old session's MID 0003";
		let revived = WaitRevived( lost, row.Slug );
		ASSERT_TRUE( revived ) << "the row's client never reconnected";
		EXPECT_TRUE( WaitUntil([&]{ return listener->Pushes>pushes; }) ) << "the new server's value";
		ASSERT_TRUE( WaitUntil([&]{ return OpText( ReadValue(revived, OpNode("Revisions")) )=="0001:2 0080:1"; }) ) << OpText( ReadValue(revived, OpNode("Revisions")) );
		EXPECT_EQ( OpCount(ReadValue(revived, OpNode("Reconnects"))), 0u ) << "a new driver's first connection";
	}

	//A row inserted while another holds its port:  the host's start fails with a line naming the port and portBase, and the row
	//stands;  the port free, the row's next change starts its host.
	TEST_F( DriverHostTests, AHeldPortIsLoggedAndTheRowStands ){
		const string slug{ "driver-collision" };
		PurgeServerCnnctn( slug );
		//The next row's id:  the one after a row made and purged now, or, where the table reuses it, that one.
		let probe = CreateRow( "driver-collision-probe", "url:$url", {{"url", OpcUrl()}} );
		ASSERT_TRUE( probe ) << probe.error();
		PurgeServerCnnctn( *probe );
		using boost::asio::ip::tcp;
		boost::asio::io_context context;
		vector<tcp::acceptor> held;
		for( let id : {*probe, *probe+1} )
			held.emplace_back( context, tcp::endpoint{boost::asio::ip::make_address("127.0.0.1"), (PortType)(ServerCnnctn::DriverPortBase()+id)} );

		Logging::Capture errors{ ELogLevel::Error };
		let created = CreateDriverRow( slug );
		ASSERT_TRUE( created ) << "the insert stands: " << created.error();
		let id = *created;
		absl::Cleanup purge = [id]{ PurgeServerCnnctn( id ); };
		ASSERT_TRUE( id==*probe || id==*probe+1 ) << id << " after " << *probe;
		let port = ServerCnnctn::DriverPortBase()+id;
		vector<Logging::Entry> logged;
		ASSERT_TRUE( WaitUntil([&]{ logged = errors.Find( [&]( const Logging::Entry& e ){ return e.Message().contains( slug ); } ); return logged.size()>0; }) ) << "no line for the host that could not start";
		let line = logged.front().Message();
		TRACE( "{}", line );
		EXPECT_TRUE( line.contains(Ƒ("127.0.0.1:{}", port)) ) << line;
		EXPECT_TRUE( line.contains(Ƒ("/gateway/drivers/portBase, {}", ServerCnnctn::DriverPortBase())) ) << line;
		EXPECT_FALSE( DriverHost::Find(slug) );
		EXPECT_TRUE( SelectServerCnnctn(id) ) << "the row was taken back";

		held.clear();
		let error = UpdateRow( id, "driver:$driver", {{"driver", "{}"}} );
		ASSERT_FALSE( error ) << *error;
		EXPECT_TRUE( WaitHost(slug) ) << "the freed port's host";
	}

	//A controller whose results are not MID 0061 is refused at the connect:  the reason in LastError and the log, and no attempt
	//after it.
	TEST_F( DriverHostTests, AnMtFocusIsRefused ){
		auto focus = Station();
		focus.Identity.SystemType = 4;
		Logging::Capture warnings{ OpenProtocol::LogTag, ELogLevel::Warning };
		Controller station{ *Executor(), ControllerPort(), move(focus) };
		const Row row{ "driver-mt-focus" };
		ASSERT_TRUE( WaitOp(row, "LastError", []( let& v ){ return OpText( v ).contains( "MT Focus 6000" ); }) ) << OpText( Op(row, "LastError") );
		EXPECT_EQ( OpFlag(Op(row, "Connected")), false );
		EXPECT_TRUE( OpText(Op(row, "Identity")).contains("\"systemType\":4") ) << OpText( Op(row, "Identity") );
		EXPECT_TRUE( WaitUntil([&]{ return station.Clients()==0; }) );
		EXPECT_EQ( RevisionsOf(station, 3).size(), 1u ) << "MID 0003 before the close";
		EXPECT_EQ( warnings.Find([&]( const Logging::Entry& e ){ return e.Message().contains( "MT Focus 6000" ) && e.Message().contains( row.Slug ); }).size(), 1u );
		std::this_thread::sleep_for( 1s );//proving a negative:  the suite's backoff tops out at half a second.
		EXPECT_EQ( RevisionsOf(station, 1).size(), 1u ) << "the driver tried the controller again";

		station.Edit( []( Controller::Script& script ){ script.Identity.SystemType = 2; } );//the row's next change is the next try.
		let heard = station.Frames().size();
		ASSERT_FALSE( UpdateRow(row.Id, "driver:$driver", {{"driver", "{}"}}) );
		ASSERT_TRUE( WaitUntil([&]{ return RevisionsOf( station, 1, heard ).size()==1; }) );
		ASSERT_TRUE( WaitOp(row, "LastError", []( let& v ){ return OpText( v ).contains( "PowerMACS" ); }) ) << OpText( Op(row, "LastError") );
	}

	//The shutdown's order, with a group subscribed (gatewayStartup.cpp's hooks):  the collectors unsubscribe, the clients close
	//their sessions, and the hosts' stop, straight behind them, waits for those sessions before a server goes - nothing logged as
	//an error, and no session cut off.  The group and the row's clients go as a purge and a delete take them, which is how the
	//historian's and the clients' hooks do, since those two would end every other suite's;  the hosts' stop is the hook's own.
	TEST_F( DriverHostTests, AShutdownStopsTheServersAfterTheirClients ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const Row row{ "driver-shutdown" };
		let group = Id( Mutate("createHistGroup( name:$name, slug:$slug, serverConnectionId:$connection ){ id }", {{"name", "Driver shutdown"}, {"slug", "driver-shutdown"}, {"connection", row.Id}}) );
		absl::Cleanup purge = [&]{ PurgeGroup( group, row ); };
		let member = AddNode( group, jvalue{jobject{ {"nsu", DriverHost::NamespaceUri(row.Slug, ServerCnnctn::KindOpenProtocol)}, {"s", "_Op.Since"} }} );
		{
			let host = Host( group );
			ASSERT_NO_THROW( WaitSubscribed(host, 1) );
			ASSERT_NO_THROW( WaitStored(host, member, Joined, 1) );
		}
		ASSERT_TRUE( WaitConnected(row) );//a web session's client beside the collector's.
		ASSERT_EQ( Sessions(row, "Certificate").size(), 1u );
		ASSERT_EQ( Sessions(row, "IssuedToken").size(), 1u );

		Logging::Capture lines{ ELogLevel::Warning };
		ASSERT_TRUE( Collector(row) );
		std::move( purge ).Invoke();//the collector unsubscribes.
		EXPECT_EQ( UAClient::ConnectionEdited(DB::Key{row.Id}, true), 2u ) << "the collector's client and the web session's";
		DriverHost::Shutdown( false );
		absl::Cleanup start = []{
			try{
				DriverHost::Start();
			}
			catch( const std::exception& e ){
				ADD_FAILURE() << "the hosts did not start again: " << e.what();
			}
		};
		EXPECT_FALSE( DriverHost::Find(row.Slug) );
		EXPECT_EQ( RevisionsOf(station, 3).size(), 1u ) << "MID 0003 before the close";
		let logged = lines.Find( []( const Logging::Entry& e ){ return e.Level>=ELogLevel::Error || e.Message().contains( "still open" ); } );
		for( let& entry : logged )
			ADD_FAILURE() << entry.Message();

		std::move( start ).Invoke();//for the suites after this one, and the row's purge.
		EXPECT_TRUE( WaitHost(row.Slug) ) << "the start read the row";
		EXPECT_TRUE( WaitConnected(row) );
	}
}