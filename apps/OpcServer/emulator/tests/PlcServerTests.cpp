//PlcServer - the emulated PLC's own UA server on the library's endpoint (servers plan Phase 6):  the /emulator/plc block and its
//defaults, the address it listens on, a startup on a taken port, a secured session with a username from the block's list reading
//and writing its nodes, the logins it refuses, the references it refuses, a client's command write reaching Drain, the contract's
//field map and the local writes the publisher samples, each in its node's type and with its source time.  One server per suite on
//a test port;  PubSubTests publishes through one too.
#include <jde/fwk/settings.h>
#include <jde/fwk/crypto/issue.h>
#include <jde/app/client/IAppClient.h>
#include <jde/opc/tests/UATestClient.h>
#include <jde/opc/uatypes/UAString.h>
#include <jde/opc/uatypes/Value.h>
#include "../PlcServer.h"

#define let const auto
namespace Jde::Opc::Emulator::Tests{
	using Opc::Tests::UATestClient;
	struct PlcServerTests : ::testing::Test{
	protected:
		Ω nodeset()ε->fs::path{
			let p = Settings::FindPath( "/emulator/plc/nodeset" ); THROW_IF( !p, "/emulator/plc/nodeset is required." );
			return *p;
		}
		Ω contract()ε->PubSub::Config{ return PubSub::Config{ Settings::AsObject("/emulator/pubsub") }; }
		Ω deviceSsl()ι->Crypto::CryptoSettings{ return Crypto::CryptoSettings{ Settings::FindDefaultObject("/emulator/ssl") }; }
		Ω clientSsl( sv block="/testing/client/ssl" )ε->Crypto::CryptoSettings{
			Crypto::CryptoSettings y{ Settings::FindDefaultObject(block) };
			Crypto::EnsureKeyCertificate( y );
			return y;
		}
		//No hub here:  IAppClient's defaults with no session answer every lookup "No connection to the AppServer" - a username
		//login resolves to no user and the session still activates (UAAccess::ResolveUser), an issued token is refused.
		Ω appClient()ι->sp<App::Client::IAppClient>{ return ms<App::Client::IAppClient>(); }
		Ω SetUpTestCase()ε->void{
			_settings = PlcServer::ReadSettings( deviceSsl() );
			_plc = mu<PlcServer>( *_settings, appClient(), nodeset(), contract() );
			_ns = _plc->Namespace( _plc->Contract()->Namespace );
			_command = _plc->Resolve( "pump1/status", _ns );
			_plc->Watch( _command, 7 );
			_plc->Run();
		}
		Ω TearDownTestCase()ι->void{ _plc.reset(); }
		Ω serverUrls( const PlcServer& plc )ι->vector<string>{
			vector<string> y;
			let& config = *UA_Server_getConfig( plc.Ptr() );
			for( size_t i=0; i<config.serverUrlsSize; ++i )
				y.push_back( ToString(config.serverUrls[i]) );
			return y;
		}
		Ω readDouble( const NodeId& node )ι->optional<double>{//a Good one's.
			let v = ReadValue( *_plc->Ptr(), node );
			return v.status==UA_STATUSCODE_GOOD && v.hasValue && UA_Variant_hasScalarType(&v.value, &UA_TYPES[UA_TYPES_DOUBLE]) ? optional{ v.Get<UA_Double>(0) } : nullopt;
		}
		//A client on the secured channel, as the gateway's collector connects:  `identity` is the login under test.
		Ω client( UA_ExtensionObject identity, const Crypto::CryptoSettings& ssl )ε->UATestClient{
			return UATestClient{ Ƒ("opc.tcp://127.0.0.1:{}", _plc->Port()), ssl, identity, UA_MESSAGESECURITYMODE_SIGNANDENCRYPT };
		}
		Ω field( uint i )ε->const NodeId&{ return _plc->Contract()->Fields.at( i ).Node; }
		template<class T> Ω readAs( const NodeId& node, const UA_DataType& type )ι->optional<T>{
			let v = ReadValue( *_plc->Ptr(), node );
			return v.hasValue && UA_Variant_hasScalarType(&v.value, &type) ? optional{ v.Get<T>(0) } : nullopt;
		}
		//A scalar variable of `type` under Objects, with no value yet, for the writes a nodeset's types take.
		Ω addVariable( UA_UInt32 id, const UA_NodeId& type )ε->NodeId{
			UA_VariableAttributes attr = UA_VariableAttributes_default;
			attr.dataType = type;
			attr.valueRank = UA_VALUERANK_SCALAR;
			let node = UA_NODEID_NUMERIC( _ns, id );
			let name = Ƒ( "typed{}", id );
			UAε( UA_Server_addVariableNode(_plc->Ptr(), node, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), UA_QUALIFIEDNAME(_ns, (char*)name.c_str()), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attr, nullptr, nullptr) );
			return NodeId{ node };
		}
		static optional<Server::UASettings> _settings;
		static up<PlcServer> _plc;
		static NsIndex _ns;
		static NodeId _command;
	};
	optional<Server::UASettings> PlcServerTests::_settings;
	up<PlcServer> PlcServerTests::_plc;
	NsIndex PlcServerTests::_ns;
	NodeId PlcServerTests::_command;

	TEST_F( PlcServerTests, TheBlockReads ){
		let& s = *_settings;
		EXPECT_EQ( s.Port, 4851 );
		EXPECT_EQ( s.Address, "127.0.0.1" );
		EXPECT_EQ( s.ApplicationName, "Jde-Cpp PlcEmulator [PlcEmulatorTests]" );
		EXPECT_EQ( s.Ssl.Certificate.CommonName, "PlcEmulatorTests.plc" );
		EXPECT_EQ( s.Ssl.Certificate.SanUri(), "urn:jde:plc-emulator-tests" );
		EXPECT_EQ( s.TrustedCertDirs.size(), 1u );
		EXPECT_FALSE( s.Tokens.Anonymous );
		EXPECT_TRUE( s.Tokens.Certificate );
		EXPECT_TRUE( s.Tokens.Issued );
		ASSERT_EQ( s.Users.size(), 1u );
		EXPECT_EQ( s.Users[0].first, "plc" );
		EXPECT_TRUE( s.None );
		EXPECT_EQ( s.Limit.MaxSessions, optional<uint16>{20} );
		EXPECT_EQ( s.Limit.MaxSecureChannels, optional<uint16>{25} );
		EXPECT_FALSE( s.Limit.MaxMonitoredItems );
	}
	//No block:  4842 on loopback, the device's own pair, this product's clients and rejected dirs, no logins but the hub's.
	TEST_F( PlcServerTests, DefaultsWithoutTheBlock ){
		let s = PlcServer::ReadSettings( deviceSsl(), "/emulator/noSuchBlock" );
		EXPECT_EQ( s.Port, 4842 );
		EXPECT_EQ( s.Address, "127.0.0.1" );
		EXPECT_EQ( s.Ssl.Certificate.Path, deviceSsl().Certificate.Path );
		ASSERT_EQ( s.TrustedCertDirs.size(), 1u );
		EXPECT_EQ( s.TrustedCertDirs[0].filename(), "clients" );
		EXPECT_EQ( s.RejectedCertDir.filename(), "rejected" );
		EXPECT_TRUE( s.Users.empty() );
		EXPECT_FALSE( s.Limit.MaxSessions );
		EXPECT_FALSE( s.Limit.MaxSecureChannels );
	}
	TEST_F( PlcServerTests, ListensOnTheAddress ){
		EXPECT_EQ( serverUrls(*_plc), vector<string>{ Ƒ("opc.tcp://127.0.0.1:{}", _plc->Port()) } );
	}
	TEST_F( PlcServerTests, AnEmptyAddressIsEveryInterface ){//a second server, configured and not run:  the urls are the config's.
		auto settings = *_settings;
		settings.Port = 4853;
		settings.Address.clear();
		PlcServer wide{ settings, appClient(), nodeset(), contract() };
		EXPECT_EQ( serverUrls(wide), vector<string>{"opc.tcp://:4853"} );
	}
	//A startup that can't listen - here an address no interface has (TEST-NET-1), as a port another program holds;  a second
	//open62541 server shares one, since it listens SO_REUSEPORT:  Run throws, and the server deletes clean - no writer made, its
	//event loop stopped - where a stranded PubSub component aborted UA_Server_delete's assert.
	TEST_F( PlcServerTests, AFailedStartupDeletesClean ){
		auto settings = *_settings;
		settings.Port = 4854;
		settings.Address = "192.0.2.1";
		PlcServer unreachable{ settings, appClient(), nodeset(), contract() };
		EXPECT_ANY_THROW( unreachable.Run() );
	}
	TEST_F( PlcServerTests, FindFieldFollowsTheContract ){
		ASSERT_EQ( _plc->Contract()->Fields.size(), 5u );
		EXPECT_EQ( _plc->FindField("pump1.motorRpm"), optional<uint>{0} );
		EXPECT_EQ( _plc->FindField("pumpManual.motorRpm"), optional<uint>{4} );
		EXPECT_FALSE( _plc->FindField("pump1.status") );//subscribed, never published
		EXPECT_FALSE( _plc->FindField("nothing.here") );
	}
	TEST_F( PlcServerTests, WriteLandsInTheLocalNodeThePublisherSamples ){
		_plc->Write( 0, 1234.5 );
		_plc->Write( 4, 42 );
		EXPECT_EQ( readDouble(field(0)), optional<double>{1234.5} );
		EXPECT_EQ( readDouble(field(4)), optional<double>{42} );
	}
	//The quality is stored with the value - Bad included, because the OpcServer's reader skips a field without one - and
	//the next Good write replaces it.
	TEST_F( PlcServerTests, WriteCarriesTheStatus ){
		let& node = field( 1 );
		constexpr UA_StatusCode pinnedHigh{ UA_STATUSCODE_UNCERTAINENGINEERINGUNITSEXCEEDED | 0x0400 | 0x0200 };//InfoType DataValue + LimitBits High
		for( let status : {pinnedHigh, (UA_StatusCode)UA_STATUSCODE_BADSENSORFAILURE, (UA_StatusCode)UA_STATUSCODE_GOOD} ){
			_plc->Write( 1, 777, status );
			let dv = ReadValue( *_plc->Ptr(), node );//the whole DataValue, as the publisher samples it.
			EXPECT_EQ( dv.status, status ) << UA_StatusCode_name( status );
			ASSERT_TRUE( dv.hasValue && UA_Variant_hasScalarType(&dv.value, &UA_TYPES[UA_TYPES_DOUBLE]) ) << UA_StatusCode_name( status );
			EXPECT_EQ( dv.Get<UA_Double>(0), 777 );
		}
	}
	TEST_F( PlcServerTests, WriteRefusesAnUnknownField ){
		EXPECT_ANY_THROW( _plc->Write(99, 1) );
	}
	//A held value reads back with the time it was written, not each read's.
	TEST_F( PlcServerTests, WriteCarriesItsSourceTime ){
		let& node = field( 3 );
		let before = UA_DateTime_now();
		_plc->Write( 3, 55 );
		let first = ReadValue( *_plc->Ptr(), node, UA_TIMESTAMPSTORETURN_SOURCE );
		std::this_thread::sleep_for( 5ms );
		let second = ReadValue( *_plc->Ptr(), node, UA_TIMESTAMPSTORETURN_SOURCE );
		ASSERT_TRUE( first.hasSourceTimestamp && second.hasSourceTimestamp );
		EXPECT_GE( first.sourceTimestamp, before );
		EXPECT_EQ( first.sourceTimestamp, second.sourceTimestamp );
	}
	//Under -transport=server a tag is written as its node's type:  an integer rounded and held to its range, a Float, and a Double
	//for an abstract DataType;  a String node is refused when it is resolved, not on every cycle.
	TEST_F( PlcServerTests, AWriteTakesTheNodesType ){
		let count = addVariable( 90001, UA_TYPES[UA_TYPES_UINT32].typeId );
		let& countType = _plc->ValueType( count );
		ASSERT_EQ( &countType, &UA_TYPES[UA_TYPES_UINT32] );
		_plc->Write( count, countType, 41.6 );
		EXPECT_EQ( readAs<UA_UInt32>(count, countType), optional<UA_UInt32>{42} );
		_plc->Write( count, countType, -5 );
		EXPECT_EQ( readAs<UA_UInt32>(count, countType), optional<UA_UInt32>{0} );
		_plc->Write( count, countType, 5e9 );
		EXPECT_EQ( readAs<UA_UInt32>(count, countType), optional<UA_UInt32>{UINT32_MAX} );
		let reason = addVariable( 90002, UA_TYPES[UA_TYPES_INT32].typeId );
		_plc->Write( reason, _plc->ValueType(reason), -7.5 );
		EXPECT_EQ( readAs<UA_Int32>(reason, UA_TYPES[UA_TYPES_INT32]), optional<UA_Int32>{-8} );
		let speed = addVariable( 90003, UA_TYPES[UA_TYPES_FLOAT].typeId );
		_plc->Write( speed, _plc->ValueType(speed), 1.5 );
		EXPECT_EQ( readAs<UA_Float>(speed, UA_TYPES[UA_TYPES_FLOAT]), optional<UA_Float>{1.5f} );
		let number = addVariable( 90004, UA_NODEID_NUMERIC(0, UA_NS0ID_NUMBER) );
		EXPECT_EQ( &_plc->ValueType(number), &UA_TYPES[UA_TYPES_DOUBLE] );
		_plc->Write( number, _plc->ValueType(number), 2.25 );
		EXPECT_EQ( readDouble(number), optional<double>{2.25} );
		let text = addVariable( 90005, UA_TYPES[UA_TYPES_STRING].typeId );
		EXPECT_ANY_THROW( _plc->ValueType(text) );
		EXPECT_ANY_THROW( _plc->Write(text, UA_TYPES[UA_TYPES_STRING], 1) );
	}
	//The transport a gateway group collects over:  SignAndEncrypt with a trusted client certificate, a login from the block's list,
	//and every node open to the session - the emulator's reading comes back, and a write lands as the write-through's would.
	TEST_F( PlcServerTests, AUsernameSessionReadsAndWritesOnASecuredChannel ){
		auto c = client( UATestClient::Username("plc", "plc"), clientSsl() );
		let sc = c.Connect(); ASSERT_EQ( sc, UA_STATUSCODE_GOOD ) << UA_StatusCode_name( sc );
		_plc->Write( 0, 321.5 );
		let read = c.Read( field(0) );
		ASSERT_EQ( read.status, UA_STATUSCODE_GOOD ) << UA_StatusCode_name( read.status );
		ASSERT_TRUE( read.hasValue && UA_Variant_hasScalarType(&read.value, &UA_TYPES[UA_TYPES_DOUBLE]) );
		EXPECT_EQ( read.Get<UA_Double>(0), 321.5 );
		UA_Double written{ 654.25 };
		let wsc = c.Write( field(2), &written, UA_TYPES[UA_TYPES_DOUBLE] );
		EXPECT_EQ( wsc, UA_STATUSCODE_GOOD ) << UA_StatusCode_name( wsc );
		EXPECT_EQ( readDouble(field(2)), optional<double>{654.25} );
	}
	//A session writes a PLC's values, not its address space:  AddReferences needs Update, which PlcNodeAccess gives nobody.
	TEST_F( PlcServerTests, AReferenceIsRefused ){
		auto c = client( UATestClient::Username("plc", "plc"), clientSsl() );
		let sc = c.Connect(); ASSERT_EQ( sc, UA_STATUSCODE_GOOD ) << UA_StatusCode_name( sc );
		UA_ExpandedNodeId target;
		UA_ExpandedNodeId_init( &target );
		target.nodeId = _command;//borrowed:  nothing here is cleared.
		let add = UA_Client_addReference( c, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), true, UA_STRING_NULL, target, UA_NODECLASS_VARIABLE );
		EXPECT_EQ( add, UA_STATUSCODE_BADUSERACCESSDENIED ) << UA_StatusCode_name( add );
	}
	TEST_F( PlcServerTests, AnonymousIsRefused ){//the block's default:  a session is a hub user.
		auto c = client( UATestClient::Anonymous(), clientSsl() );
		let sc = c.Connect(); EXPECT_NE( sc, UA_STATUSCODE_GOOD ) << UA_StatusCode_name( sc );
	}
	TEST_F( PlcServerTests, AnIssuedTokenIsAskedOfTheHub ){//which this suite has none of, so the token is refused:  the login is the hub's, not a list of this process's own.
		auto c = client( UATestClient::Issued("1a"), clientSsl() );
		let sc = c.Connect(); EXPECT_NE( sc, UA_STATUSCODE_GOOD ) << UA_StatusCode_name( sc );
	}
	TEST_F( PlcServerTests, AStrangersCertificateIsRefused ){//a pair from a directory the block does not trust gets no channel, the right login or not.
		auto c = client( UATestClient::Username("plc", "plc"), clientSsl("/testing/stranger/ssl") );
		let sc = c.Connect(); EXPECT_NE( sc, UA_STATUSCODE_GOOD ) << UA_StatusCode_name( sc );
	}
	//A command tag under -transport=server:  a client's Boolean write to the watched node is queued with its key for the emulator's
	//thread;  a write of another type is refused by the server and queues nothing, and the emulator's own writes of other nodes too.
	TEST_F( PlcServerTests, ACommandWriteIsDrained ){
		auto c = client( UATestClient::Username("plc", "plc"), clientSsl() );
		let sc = c.Connect(); ASSERT_EQ( sc, UA_STATUSCODE_GOOD ) << UA_StatusCode_name( sc );
		EXPECT_TRUE( _plc->Drain().empty() );
		UA_Boolean off{ false };
		let wsc = c.Write( _command, &off, UA_TYPES[UA_TYPES_BOOLEAN] );
		ASSERT_EQ( wsc, UA_STATUSCODE_GOOD ) << UA_StatusCode_name( wsc );
		let commands = _plc->Drain();
		ASSERT_EQ( commands.size(), 1u );
		EXPECT_EQ( commands[0].Key, 7u );
		EXPECT_FALSE( commands[0].Value );
		EXPECT_EQ( commands[0].User.Value, 0u );//no hub:  the username resolved to no user.
		EXPECT_TRUE( _plc->Drain().empty() );
		UA_Double notACommand{ 1 };
		EXPECT_NE( c.Write(_command, &notACommand, UA_TYPES[UA_TYPES_DOUBLE]), UA_STATUSCODE_GOOD );
		_plc->Write( 0, 1 );
		EXPECT_TRUE( _plc->Drain().empty() );
	}
	//Watch hands back the node's value, where a command starts under -transport=server, as the first notification is under the
	//others;  a node has one command tag.
	TEST_F( PlcServerTests, WatchReturnsTheNodesValue ){
		let node = _plc->Resolve( "pump2/status", _ns );
		UA_Boolean off{ false };
		UA_Variant v;
		UA_Variant_setScalar( &v, &off, &UA_TYPES[UA_TYPES_BOOLEAN] );
		_plc->Write( node, v );//what Watch reads back.
		EXPECT_EQ( _plc->Watch(node, 8), optional<bool>{false} );
		EXPECT_ANY_THROW( _plc->Watch(node, 9) );
		EXPECT_ANY_THROW( _plc->Watch(_command, 10) );
	}
}