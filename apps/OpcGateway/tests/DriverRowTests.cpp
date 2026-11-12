#include <jde/fwk/process/execution.h>
#include <jde/db/IDataSource.h>
#include <jde/tests/HttpTest.h>
#include "../src/opcInternal.h"
#include "../src/GatewayAppClient.h"
#include "../src/types/OpenProtocolRow.h"
#include "utils/ClientFixture.h"
#include "utils/DriverRows.h"
#include "utils/GatewayClientSocket.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	using OpenProtocol::Controller;

	//docs/open-protocol/plan.md Phase 3 (#465):  a connection row of a kind with a driver - `kind` and `driver` through the insert
	//proc, the QL and ServerCnnctn, the kind's save rules, the dial of the driver's server in place of the url, and testConnection
	//on an Open Protocol row against the library's test controller.  The row's server itself is DriverHostTests'.
	struct DriverRowTests : ClientFixture{};

	constexpr sv Driver{ R"({"timeZone":"Europe/Stockholm","revisions":{"0061":3}})" };
	//Whether the row stores none of the four columns a kind with a driver fixes.
	Ω storesNothingFixed( ServerCnnctnPK id )ε->bool{
		return DS()->ScalerSync<uint>( {Ƒ("select count(*) from {} where server_connection_id={} and certificate_uri is null and default_browse_ns is null and security_mode is null and security_policy is null", GetViewPtr("server_connections")->DBName, id)} )==1;
	}
	Ω ownUri()ε->string{//the gateway's own applicationUri.
		let& ssl = AppClient()->SslSettings; THROW_IF( !ssl, "the app client has no ssl settings." );
		return ssl->Certificate.SanUri();
	}

	TEST_F( DriverRowTests, ARowKeepsItsKindAndDriver ){
		const string slug{ "driver-round-trip" };
		PurgeServerCnnctn( slug );
		let created = CreateRow( slug, "url:$url, kind:\"open-protocol\", driver:$driver", {{"url", ControllerUrl()}, {"driver", Driver}} );
		ASSERT_TRUE( created ) << created.error();
		let id = *created;
		absl::Cleanup purge = [id]{ PurgeServerCnnctn( id ); };
		//The QL's own select:  the two as saved, and none of what the kind fixes.
		let stored = QL().QuerySync<jobject>( Ƒ("serverConnection( id:{} ){{ kind driver certificateUri defaultBrowseNs securityMode securityPolicy }}", id), {}, {UserPK::System} );
		EXPECT_EQ( Json::FindDefaultSV(stored, "kind"), ServerCnnctn::KindOpenProtocol ) << serialize( stored );
		EXPECT_EQ( Json::FindDefaultSV(stored, "driver"), Driver ) << serialize( stored );
		for( sv fixed : {"certificateUri"sv, "defaultBrowseNs"sv, "securityMode"sv, "securityPolicy"sv} ){
			let value = stored.if_contains( fixed );
			EXPECT_TRUE( !value || value->is_null() ) << fixed << ": " << serialize( stored );
		}
		EXPECT_TRUE( storesNothingFixed(id) );
		//ServerCnnctn, from the row and from the QL's json:  the driver server's uri, the gateway's own, and its one namespace.
		let uri = ownUri();
		ASSERT_FALSE( uri.empty() );
		let check = [&]( const ServerCnnctn& row, sv where ){
			EXPECT_EQ( row.Kind, ServerCnnctn::KindOpenProtocol ) << where;
			EXPECT_EQ( row.Driver, Driver ) << where;
			EXPECT_TRUE( row.HasDriver() ) << where;
			EXPECT_EQ( row.CertificateUri, uri ) << where;
			EXPECT_EQ( row.DefaultBrowseNs, ServerCnnctn::DriverBrowseNs ) << where;
			EXPECT_EQ( row.EffectiveMode(), ServerCnnctn::ModeSignAndEncrypt ) << where;
		};
		let row = RowOf( id );
		check( row, "row" );
		check( *SelectServerCnnctn(id), "ql" );
		let portBase = Settings::FindNumber<uint>( "/gateway/drivers/portBase" ).value_or( 0 );
		EXPECT_EQ( portBase, 4980u ) << "the suite's, clear of the shipped 4870";
		EXPECT_EQ( row.DriverUrl(), Ƒ("opc.tcp://127.0.0.1:{}", portBase+id) );
		const OpenProtocolRow read{ row };
		EXPECT_EQ( read.Host, "127.0.0.1" );
		EXPECT_EQ( read.Port, ControllerPort() );
		ASSERT_TRUE( read.TimeZone );
		EXPECT_EQ( read.TimeZone->name(), "Europe/Stockholm" );
		EXPECT_EQ( read.Revisions.size(), 1u );
		EXPECT_EQ( read.Revisions.at(61), 3 );

		//A UA row has neither, and keeps what it stores.
		let ua = GetConnection( OpcServerSlug );
		EXPECT_TRUE( ua.Kind.empty() );
		EXPECT_TRUE( ua.Driver.empty() );
		EXPECT_FALSE( ua.HasDriver() );
		EXPECT_EQ( ua.CertificateUri, OpcUrn() );

		//The url's forms:  4545 where the port is left off (§2.1.1), a name, an IPv6 address in brackets.
		ServerCnnctn bare{ string{"open-protocol://controller-7.plant"} };
		EXPECT_EQ( OpenProtocolRow{bare}.Host, "controller-7.plant" );
		EXPECT_EQ( OpenProtocolRow{bare}.Port, 4545 );
		EXPECT_FALSE( OpenProtocolRow{bare}.TimeZone );
		ServerCnnctn v6{ string{"open-protocol://[::1]:4546"} };
		EXPECT_EQ( OpenProtocolRow{v6}.Host, "::1" );
		EXPECT_EQ( OpenProtocolRow{v6}.Port, 4546 );
	}

	//The save rules at the insert (ServerCnnctnMAwait's RowError):  an unknown kind;  what a kind with a driver fixes, sent anyway;
	//a url that is not the kind's;  a `driver` that is no object of timeZone and revisions.  None reaches the table.
	TEST_F( DriverRowTests, ARowItsKindRefusesIsNotSaved ){
		const string slug{ "driver-refused" };
		PurgeServerCnnctn( slug );
		let url = ControllerUrl();
		let openProtocol = [&]( sv more, string driver={} )->optional<string>{
			let created = CreateRow( slug, Ƒ("url:$url, kind:\"open-protocol\", driver:$driver{}", more), {{"url", url}, {"driver", driver}} );
			return created ? nullopt : optional<string>{ created.error() };
		};
		let withUrl = [&]( string controller )->optional<string>{
			let created = CreateRow( slug, "url:$url, kind:\"open-protocol\"", {{"url", controller}} );
			return created ? nullopt : optional<string>{ created.error() };
		};
		let expect = [&]( optional<string> error, sv expected, sv what ){
			ASSERT_TRUE( error ) << what << " was saved";
			EXPECT_NE( string::npos, error->find(expected) ) << what << " -> " << *error;
			EXPECT_FALSE( SelectServerCnnctn(slug) ) << what;
		};
		{
			let created = CreateRow( slug, "url:$url, kind:\"plc\"", {{"url", OpcUrl()}} );
			expect( created ? nullopt : optional<string>{created.error()}, "is not one of ua or open-protocol", "kind plc" );
		}
		for( let& [more, expected] : vector<std::pair<string,string>>{
			{ ", certificateUri:\"urn:x\"", "takes no certificateUri" },
			{ ", defaultBrowseNs:2", "takes no defaultBrowseNs" },
			{ ", securityMode:\"SignAndEncrypt\"", "takes no securityMode" },
			{ ", securityPolicy:\"http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256\"", "takes no securityPolicy" },
			{ ", sessionTimeout:0", "of 0" } } )//the connection settings' rules still hold.
			expect( openProtocol(more), expected, more );
		for( let& [controller, expected] : vector<std::pair<string,string>>{
			{ OpcUrl(), "does not start with the scheme" },
			{ "open-protocol://", "names no host" },
			{ "open-protocol://:4545", "names no host" },
			{ "open-protocol://controller 7", "not a name or an address" },
			{ "open-protocol://[::1", "has no ']'" },
			{ "open-protocol://controller:0", "a port, 1 to 65535" },
			{ "open-protocol://controller:65536", "a port, 1 to 65535" },
			{ "open-protocol://controller:45x5", "a port, 1 to 65535" },
			{ "open-protocol://controller/station", "not a name or an address" },
			{ "open-protocol://controller:4545/station", "a port, 1 to 65535" } } )
			expect( withUrl(controller), expected, controller );
		for( let& [driver, expected] : vector<std::pair<string,string>>{
			{ "[]", "is a json object" },
			{ "the plant's", "is a json object" },
			{ R"({"timeZone":"Europe/Stockholm","plc":1})", "has no 'plc'" },
			{ R"({"timeZone":"Mars/Olympus_Mons"})", "is not a time zone the gateway knows" },
			{ R"({"timeZone":1})", "IANA name" },
			{ R"({"revisions":[3]})", "an object of MID" },
			{ R"({"revisions":{"61":3}})", "by its four digits" },
			{ R"({"revisions":{"006x":3}})", "by its four digits" },
			{ R"({"revisions":{"0061":0}})", "from 1 to 999" },
			{ R"({"revisions":{"0061":-1}})", "from 1 to 999" },
			{ R"({"revisions":{"0061":2.5}})", "from 1 to 999" },
			{ R"({"revisions":{"0061":"3"}})", "from 1 to 999" },
			{ R"({"revisions":{"0061":1000}})", "from 1 to 999" } } )
			expect( openProtocol({}, driver), expected, driver );
		for( sv kind : {""sv, ", kind:\"ua\""sv} ){//`driver` and an Open Protocol url are a driver kind's.
			let created = CreateRow( slug, Ƒ("url:$url, driver:$driver{}", kind), {{"url", OpcUrl()}, {"driver", "{}"}} );
			expect( created ? nullopt : optional<string>{created.error()}, "takes no driver settings", kind );
			let controller = CreateRow( slug, Ƒ("url:$url{}", kind), {{"url", url}} );
			expect( controller ? nullopt : optional<string>{controller.error()}, "set the kind to open-protocol", kind );
		}
		//What it takes:  no driver, an empty one, and a kind of ua on a UA row.
		for( let& [args, driver] : vector<std::pair<string,string>>{ {"kind:\"open-protocol\"", ""}, {"kind:\"open-protocol\"", "{}"}, {"kind:\"ua\"", ""} } ){
			let created = CreateRow( slug, Ƒ("url:$url, driver:$driver, {}", args), {{"url", args.contains("ua") ? OpcUrl() : url}, {"driver", driver}} );
			ASSERT_TRUE( created ) << args << " " << driver << " -> " << created.error();
			EXPECT_EQ( RowOf(*created).HasDriver(), !args.contains("ua") );
			PurgeServerCnnctn( *created );
		}
	}

	//The url branch (UAClient::Connect):  a driver row's client dials loopback at portBase plus the row's id, never the url, with the
	//gateway's own applicationUri as the server's:  the row's own server, which presents the process's pair.  An update is judged
	//over the merged row, and one that is accepted takes the row's clients off the registry.
	TEST_F( DriverRowTests, AClientDialsTheDriverServerAndAnEditTakesItOff ){
		const string slug{ "driver-dial" };
		PurgeServerCnnctn( slug );
		let created = CreateRow( slug, "url:$url, kind:\"open-protocol\", driver:$driver", {{"url", ControllerUrl()}, {"driver", Driver}} );
		ASSERT_TRUE( created ) << created.error();
		let id = *created;
		absl::Cleanup purge = [id]{ PurgeServerCnnctn( id ); };
		let dialed = RowOf( id ).DriverUrl();
		ASSERT_TRUE( WaitHost(slug) ) << "the row's server never started";

		let cred = SessionCredential();//the server takes no anonymous session.
		let what = Attempt( slug, cred );
		ASSERT_TRUE( what.empty() ) << what;
		ASSERT_TRUE( _client );
		EXPECT_EQ( _client->ConnectUrl(), dialed );
		EXPECT_EQ( _client->Url(), ControllerUrl() ) << "the row keeps its controller's url";
		EXPECT_EQ( _client->DefaultBrowseNs(), ServerCnnctn::DriverBrowseNs );
		let negotiated = Negotiated( _client );
		EXPECT_TRUE( negotiated.ends_with("/SignAndEncrypt)") ) << negotiated;

		//Refused by the merged row's rule:  the client stays.
		for( let& [args, variables, expected] : vector<tuple<string,jobject,string>>{
			{ "driver:$driver", {{"driver", R"({"revisions":{"0061":3},"station":2})"}}, "has no 'station'" },
			{ "url:$url", {{"url", OpcUrl()}}, "does not start with the scheme" },//a UA server's url under the saved kind.
			{ "certificateUri:\"urn:x\"", {}, "takes no certificateUri" },
			{ "kind:\"ua\"", {}, "takes no driver settings" } } ){//the saved driver under a UA kind.
			let error = UpdateRow( id, args, variables );
			ASSERT_TRUE( error ) << args;
			EXPECT_NE( string::npos, error->find(expected) ) << args << " -> " << *error;
		}
		EXPECT_EQ( RowOf(id).Driver, Driver );
		EXPECT_EQ( UAClient::Find(slug, cred), _client ) << "a refused update took the client off";
		ASSERT_FALSE( UpdateRow(id, "description:\"no rule's column named\"") );
		EXPECT_EQ( UAClient::Find(slug, cred), _client );

		let edited = R"({"timeZone":"America/Chicago"})"sv;
		let error = UpdateRow( id, "driver:$driver", {{"driver", edited}} );
		ASSERT_FALSE( error ) << *error;
		EXPECT_EQ( RowOf(id).Driver, edited );
		EXPECT_FALSE( UAClient::Find(slug, cred) ) << "the pre-edit client is still registered";
		_client = nullptr;
	}

	//A kind changed to a driver's takes the four columns the kind fixes with it, so the QL's select answers null for them as it does
	//for a row created so;  back to ua, the row is a UA row with no certificate URI.
	TEST_F( DriverRowTests, AKindChangedToADriversClearsWhatTheKindFixes ){
		const string slug{ "driver-kind-change" };
		PurgeServerCnnctn( slug );
		let created = CreateRow( slug, "url:$url, certificateUri:\"urn:x\", defaultBrowseNs:3, securityMode:\"Sign\", securityPolicy:\"http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256\"", {{"url", OpcUrl()}} );
		ASSERT_TRUE( created ) << created.error();
		let id = *created;
		absl::Cleanup purge = [id]{ PurgeServerCnnctn( id ); };
		EXPECT_FALSE( storesNothingFixed(id) );

		auto error = UpdateRow( id, "kind:\"open-protocol\"" );//the url is still the UA server's.
		ASSERT_TRUE( error );
		EXPECT_NE( string::npos, error->find("does not start with the scheme") ) << *error;
		EXPECT_FALSE( storesNothingFixed(id) );
		error = UpdateRow( id, "kind:\"open-protocol\", url:$url, certificateUri:\"urn:y\"", {{"url", ControllerUrl()}} );
		ASSERT_TRUE( error );
		EXPECT_NE( string::npos, error->find("takes no certificateUri") ) << *error;

		error = UpdateRow( id, "kind:\"open-protocol\", url:$url", {{"url", ControllerUrl()}} );
		ASSERT_FALSE( error ) << *error;
		EXPECT_TRUE( storesNothingFixed(id) );
		let row = RowOf( id );
		EXPECT_TRUE( row.HasDriver() );
		EXPECT_EQ( row.CertificateUri, ownUri() );
		EXPECT_EQ( row.DefaultBrowseNs, ServerCnnctn::DriverBrowseNs );
		EXPECT_TRUE( row.SecurityMode.empty() );

		//A secured mode on the way back needs the row's own certificate URI, not the one ServerCnnctn supplied the driver row.
		error = UpdateRow( id, "kind:\"ua\", url:$url, securityMode:\"Sign\"", {{"url", OpcUrl()}} );
		ASSERT_TRUE( error );
		EXPECT_NE( string::npos, error->find("certificate URI") ) << *error;
		error = UpdateRow( id, "kind:null, url:$url", {{"url", OpcUrl()}} );
		ASSERT_FALSE( error ) << *error;
		let ua = RowOf( id );
		EXPECT_FALSE( ua.HasDriver() );
		EXPECT_TRUE( ua.CertificateUri.empty() );
		EXPECT_EQ( ua.DefaultBrowseNs, 1 );

		//A form clearing a field the kind hides:  the UA row's own Sign is no rule's once the kind has a driver.
		ASSERT_FALSE( UpdateRow(id, "certificateUri:\"urn:x\", securityMode:\"Sign\"") );
		error = UpdateRow( id, "kind:\"open-protocol\", url:$url, certificateUri:null", {{"url", ControllerUrl()}} );
		ASSERT_FALSE( error ) << *error;
		EXPECT_TRUE( storesNothingFixed(id) );
	}

	//testConnection on an Open Protocol row (TestConnectionQLAwait.h):  a session that starts communication under the row's
	//ceilings and stops it, answering what MID 0002 said, with no client built and nothing registered;  a controller that is not
	//there is the mutation's own error, the socket's.
	TEST_F( DriverRowTests, TestConnectionReportsTheControllersIdentity ){
		const string slug{ "driverTestConnection" };//no such row:  the settings are not saved.
		ASSERT_FALSE( SelectServerCnnctn({slug}) );
		const string q{ "testConnection( opc: $opc, url: $url, kind: $kind, driver: $driver ){ controllerName supplierCode openProtocolVersion controllerSoftwareVersion toolSoftwareVersion controllerSerialNumber systemType systemSubtype stationId stationName }" };
		let variables = [&]( jvalue driver ){ return jobject{ {"opc", slug}, {"url", ControllerUrl()}, {"kind", ServerCnnctn::KindOpenProtocol}, {"driver", move(driver)} }; };
		let test = [&]( jvalue driver={} ){ return Socket().QuerySync( string{q}, variables(move(driver)) ).as_object(); };
		let null = []( const jobject& o, sv field ){ let value = o.if_contains( field ); return value && value->is_null(); };
		{
			Controller::Script script{ .Identity={ .CellId=1, .ChannelId=2, .ControllerName="Line 3 station 7", .SupplierCode="ACT", .OpenProtocolVersion="2.20.1", .ControllerSoftwareVersion="3.11.5", .ToolSoftwareVersion="1.4.2",
				.RbuType="Gold", .ControllerSerialNumber="C00123456", .SystemType=3, .SystemSubtype=1, .SequenceNumberSupport=1, .LinkingSupport=0, .StationId=7, .StationName="Station 7", .ClientId=1 } };
			script.Revisions[1] = 3;//MID 0001 at 3 and no higher:  MID 0004 code 97 above it.
			Controller station{ *Executor(), ControllerPort(), move(script) };

			let at3 = test();
			TRACE( "testConnection at 3: {}.", serialize(at3) );
			EXPECT_EQ( Json::FindDefaultSV(at3, "controllerName"), "Line 3 station 7" ) << serialize( at3 );
			EXPECT_EQ( Json::FindDefaultSV(at3, "supplierCode"), "ACT" );
			EXPECT_EQ( Json::FindDefaultSV(at3, "openProtocolVersion"), "2.20.1" );
			EXPECT_EQ( Json::FindDefaultSV(at3, "controllerSoftwareVersion"), "3.11.5" );
			EXPECT_EQ( Json::FindDefaultSV(at3, "toolSoftwareVersion"), "1.4.2" );
			for( sv later : {"controllerSerialNumber"sv, "systemType"sv, "systemSubtype"sv, "stationId"sv, "stationName"sv} )
				EXPECT_TRUE( null(at3, later) ) << later << " is revision 4's or later: " << serialize( at3 );
			EXPECT_EQ( RevisionsOf(station, 1), (vector<uint16_t>{6, 5, 4, 3}) ) << "the step-down";
			EXPECT_EQ( RevisionsOf(station, 3).size(), 1u ) << "MID 0003 before the close";
			EXPECT_TRUE( WaitUntil([&]{ return station.Clients()==0; }) ) << "the session is closed";
			EXPECT_FALSE( UAClient::ConnectionCounts().contains(slug) ) << "a test connect leaves no client registered";
			EXPECT_FALSE( UAClient::ConnectErrors().contains(slug) );

			auto heard = station.Frames().size();
			let capped = test( R"({"revisions":{"0001":2}})" );//the row's ceiling:  MID 0001 starts under it.
			EXPECT_EQ( RevisionsOf(station, 1, heard), (vector<uint16_t>{2}) );
			EXPECT_EQ( Json::FindDefaultSV(capped, "supplierCode"), "ACT" ) << serialize( capped );
			EXPECT_TRUE( null(capped, "openProtocolVersion") ) << "revision 3's: " << serialize( capped );

			station.Edit( []( Controller::Script& edit ){ edit.Revisions.clear(); } );//every revision the codec has:  MID 0002 at 6.
			heard = station.Frames().size();
			let at6 = test();
			EXPECT_EQ( RevisionsOf(station, 1, heard), (vector<uint16_t>{6}) );
			EXPECT_EQ( Json::FindDefaultSV(at6, "controllerSerialNumber"), "C00123456" ) << serialize( at6 );
			EXPECT_EQ( Json::FindNumber<uint>(at6, "systemType"), optional<uint>{3} );
			EXPECT_EQ( Json::FindNumber<uint>(at6, "systemSubtype"), optional<uint>{1} );
			EXPECT_EQ( Json::FindNumber<uint>(at6, "stationId"), optional<uint>{7} );
			EXPECT_EQ( Json::FindDefaultSV(at6, "stationName"), "Station 7" );
			{//only what is selected.
				let narrow = Socket().QuerySync( "testConnection( opc: $opc, url: $url, kind: $kind ){ controllerName }", {{"opc", slug}, {"url", ControllerUrl()}, {"kind", ServerCnnctn::KindOpenProtocol}} ).as_object();
				EXPECT_EQ( narrow.size(), 1u ) << serialize( narrow );
			}
			//The save's rules:  a row Save would refuse is refused here, with nothing dialed.
			heard = station.Frames().size();
			for( let& [more, expected] : vector<std::pair<jobject,string>>{
				{ {{"driver", R"({"timeZone":"Mars/Olympus_Mons"})"}}, "is not a time zone the gateway knows" },
				{ {{"url", OpcUrl()}}, "does not start with the scheme" },
				{ {{"kind", "plc"}}, "is not one of ua or open-protocol" },
				{ {{"kind", "ua"}}, "set the kind to open-protocol" } } ){
				auto refused = variables( nullptr );
				for( let& member : more )
					refused[member.key()] = member.value();
				try{
					Socket().QuerySync( string{q}, move(refused) );
					ADD_FAILURE() << "tested: " << serialize( more );
				}
				catch( const GatewayErrorResponse& e ){
					EXPECT_NE( string::npos, string{e.what()}.find(expected) ) << serialize( more ) << " -> " << e.what();
				}
			}
			EXPECT_THROW( Socket().QuerySync("testConnection( opc: $opc, url: $url, kind: $kind, certificateUri: \"urn:x\" ){ controllerName }", variables(nullptr)), GatewayErrorResponse );
			EXPECT_EQ( station.Frames().size(), heard );
		}
		//Nothing listens now.
		try{
			test();
			ADD_FAILURE() << "a test of a controller that is not there succeeded";
		}
		catch( const GatewayErrorResponse& e ){
			TRACE( "testConnection refused: {}.", e.what() );
			EXPECT_NE( string::npos, string{e.what()}.find(Ƒ("The controller at 127.0.0.1:{} did not start communication", ControllerPort())) ) << e.what();
			EXPECT_NE( string::npos, string{e.what()}.find("refused") ) << "the socket's error: " << e.what();
		}
		try{//over http, as the page sends it:  the reason is the body's.
			HttpTest::Post( GatewayPort(), "/graphql", serialize(jobject{{"query", q}, {"variables", variables(nullptr)}}), Ƒ("{:x}", AppClient()->SessionId()) );
			ADD_FAILURE() << "a test of a controller that is not there succeeded";
		}
		catch( const Web::Client::ClientHttpResException& e ){
			EXPECT_EQ( e.Status(), boost::beast::http::status::bad_gateway ) << e.Res().Body();
			EXPECT_TRUE( e.Res().Body().contains("did not start communication") ) << e.Res().Body();
		}
		EXPECT_FALSE( UAClient::ConnectionCounts().contains(slug) );
		EXPECT_FALSE( UAClient::ConnectErrors().contains(slug) ) << "a failed test is not the slug's connectionStatus";
	}

	//The row's two columns and testConnection's result, as the web reads them.
	TEST_F( DriverRowTests, TheIntrospectionNamesTheKindAndTheIdentity ){
		let fields = []( sv typeName ){
			let type = Socket().QuerySync( Ƒ("__type( name: \"{}\" ){{ fields{{ name type{{ name kind ofType{{ name kind }} }} }} }}", typeName) );
			return Json::AsArray( type.as_object(), "fields" );
		};
		let has = []( const jarray& rows, sv name ){ return std::ranges::any_of( rows, [&]( let& row ){ return Json::AsSV(row.as_object(), "name")==name; } ); };
		let row = fields( "ServerConnection" );
		for( sv column : {"kind"sv, "driver"sv} )
			EXPECT_TRUE( has(row, column) ) << column << ": " << serialize( row );
		for( sv typeName : {"TestConnection"sv, "testConnection"sv} ){
			let result = fields( typeName );
			for( sv field : {"applicationUri"sv, "securityMode"sv, "namespaces"sv, "controllerName"sv, "supplierCode"sv, "openProtocolVersion"sv, "controllerSoftwareVersion"sv, "toolSoftwareVersion"sv, "controllerSerialNumber"sv, "systemType"sv, "systemSubtype"sv, "stationId"sv, "stationName"sv} )
				EXPECT_TRUE( has(result, field) ) << typeName << "." << field << ": " << serialize( result );
		}
		EXPECT_EQ( fields("Namespace").size(), 2u );
	}
}