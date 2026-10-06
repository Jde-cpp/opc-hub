//Historian 2A (#209):  OpcServer collects the variables its nodesets mark Historizing through open62541's setValue and
//serves them to a UA client's HistoryRead - here open62541's synchronous client helpers, over a session of this
//program's own identity.
#include <fstream>
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/certificategroup_default.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/db/meta/AppSchema.h>//GetSchema().Authorizer
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/BrowsePath.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/uatypes/Value.h>
#include "../src/access/OpcAuthorize.h"
#include "../src/access/UAAccess.h"
#define let const auto

namespace Jde::Opc::Server::Tests{
	using namespace std::chrono;
	using Pages = vector<vector<Value>>;
	namespace{
		Ω nodeset()ε->fs::path{
			for( let& p : Settings::FindPathArray("/opcServer/configFiles") ){
				if( p.filename()=="pumps.NodeSet2.xml" )
					return p;
			}
			THROW( "pumps.NodeSet2.xml is not in /opcServer/configFiles." );
		}
		Ω doubles( const vector<Value>& values )ι->vector<double>{
			vector<double> y;
			for( let& v : values )
				y.push_back( v.hasValue && UA_Variant_hasScalarType(&v.value, &UA_TYPES[UA_TYPES_DOUBLE]) ? v.Get<UA_Double>(0) : std::numeric_limits<double>::quiet_NaN() );
			return y;
		}
		Ω all( const Pages& pages )ι->vector<Value>{
			vector<Value> y;
			for( let& page : pages ){
				for( let& value : page )
					y.emplace_back( value );
			}
			return y;
		}
		struct Reading final{ UA_StatusCode Status; Pages Values; };
		struct Request final{ optional<TimePoint> Start; optional<TimePoint> End; bool Bounds{}; UA_UInt32 Limit{}; UA_TimestampsToReturn Timestamps{ UA_TIMESTAMPSTORETURN_BOTH }; uint MaxPages{ std::numeric_limits<uint>::max() }; };
	}

	struct HistoryTests : ::testing::Test{
	protected:
		//As opcServerStartup does:  the nodesets, the history they mark, the rights, and the listener.
		Ω Start( const fs::path& pumps )ε->void{
			Server::Initialize( GetSchemaPtr() );
			auto& ua = GetUAServer();
			ua.Load( pumps );
			ua.History().Load( ua );
			static_cast<OpcAuthorize&>( *GetSchema().Authorizer ).AssignRights( ua );
			ua.Run();
			_ns = NamespaceIndex( ua, "urn:jde:pumps" );
		}
		Ω SetUpTestCase()ε->void{
			Server::Initialize( GetSchemaPtr() );//lets go of the path, and takes it again:  what an earlier run left goes, but for the lock.
			let path = Settings::FindPath( "/opcServer/hist/path" );
			THROW_IF( !path, "The tests' config has no /opcServer/hist/path." );
			for( let& entry : fs::directory_iterator{*path} ){
				if( entry.path().filename()!="historian.lock" )
					fs::remove_all( entry.path() );
			}
			_base = floor<seconds>( Clock::now() )-1min;
			Start( nodeset() );
			Connect();
		}
		Ω TearDownTestCase()ι->void{ Disconnect(); }

		//A session of this program's own user:  its session with the AppServer as an issued token, which the server takes
		//under an encrypting token policy, so the client carries the policies, with the server's own certificate for one.
		Ω Connect()ε->void{
			UA_ClientConfig config{};
			const Crypto::CryptoSettings ssl{ *Settings::FindObject("/opcServer/ssl") };
			auto certificate = ToUAByteString( Crypto::ReadCertificate(ssl.Certificate.Path) );
			auto pem = ToUAByteString( Crypto::ReadPrivateKey(ssl.PrivateKey) );
			UA_ByteString key = UA_BYTESTRING_NULL;
			UAε( UA_CertificateUtils_decryptPrivateKey(*pem, UA_BYTESTRING_NULL, &key) );
			let configured = UA_ClientConfig_setDefaultEncryption( &config, *certificate, key, nullptr, 0, nullptr, 0 );
			UA_ByteString_clear( &key );
			UAε( configured );
			config.certificateVerification.clear( &config.certificateVerification );
			UA_CertificateGroup_AcceptAll( &config.certificateVerification );
			config.securityMode = UA_MESSAGESECURITYMODE_NONE;
			UA_String_clear( &config.securityPolicyUri );
			UA_String_copy( &UA_SECURITY_POLICY_NONE_URI, &config.securityPolicyUri );
			UA_String_clear( &config.clientDescription.applicationUri );
			config.clientDescription.applicationUri = UA_STRING_ALLOC( ssl.Certificate.SanUri().c_str() );
			auto token = UA_IssuedIdentityToken_new();
			token->tokenData = UA_BYTESTRING_ALLOC( Ƒ("{:x}", AppClient()->SessionId()).c_str() );
			UA_ExtensionObject_clear( &config.userIdentityToken );
			UA_ExtensionObject_setValue( &config.userIdentityToken, token, &UA_TYPES[UA_TYPES_ISSUEDIDENTITYTOKEN] );
			_client = UA_Client_newWithConfig( &config );
			THROW_IF( !_client, "UA_Client_newWithConfig failed." );
			UAε( UA_Client_connect(_client, Ƒ("opc.tcp://127.0.0.1:{}", Settings::FindNumber<PortType>("/opcServer/port").value_or(4840)).c_str()) );
		}
		Ω Disconnect()ι->void{
			if( auto client = std::exchange(_client, nullptr) ){
				UA_Client_disconnect( client );
				UA_Client_delete( client );
			}
		}

		Ω Node( UA_UInt32 id )ι->NodeId{ return NodeId{ _ns, id }; }
		Ω History()ι->UAHistory&{ return GetUAServer().History(); }
		Ω Index( UA_UInt32 id )ι->optional<Hist::NodeIndex>{ return History().Find( Node(id) ); }
		Ω Thresholds( UA_UInt32 id )ε->Hist::Thresholds{
			let index = Index( id );
			THROW_IF( !index, "{} is not historized.", id );
			return *History().Group()->FindThresholds( *index );
		}
		//A write as a client's or the PubSub reader's reaches the server, with the time the source gave it.
		Ω Write( UA_UInt32 id, double value, TimePoint source )ε->void{
			let node = Node( id );
			UA_WriteValue write; UA_WriteValue_init( &write );
			write.nodeId = node;
			write.attributeId = UA_ATTRIBUTEID_VALUE;
			UA_Variant_setScalar( &write.value.value, &value, &UA_TYPES[UA_TYPES_DOUBLE] );
			write.value.hasValue = true;
			write.value.sourceTimestamp = UADateTime{ source }.UA();
			write.value.hasSourceTimestamp = true;
			UAε( UA_Server_write(GetUAServer().Ptr(), &write) );
		}
		//UA_Client_HistoryRead_raw, each page it fetched:  it follows the continuation points until none is left, or
		//MaxPages are in, which releases the one it holds.
		Ω Read( const NodeId& node, Request request )ι->Reading{
			struct Context final{ Pages Values; uint MaxPages; };
			Context context{ {}, request.MaxPages };
			let onPage = []( UA_Client*, const UA_NodeId*, UA_Boolean /*more*/, const UA_ExtensionObject* data, void* context )->UA_Boolean{
				auto& y = *static_cast<Context*>( context );
				auto& page = y.Values.emplace_back();
				if( data->encoding==UA_EXTENSIONOBJECT_DECODED && data->content.decoded.type==&UA_TYPES[UA_TYPES_HISTORYDATA] ){
					let& history = *static_cast<const UA_HistoryData*>( data->content.decoded.data );
					for( uint i=0; i<history.dataValuesSize; ++i )
						page.emplace_back( history.dataValues[i] );
				}
				return y.Values.size()<y.MaxPages;
			};
			let ticks = []( optional<TimePoint> t ){ return t ? UADateTime{ *t }.UA() : UA_DateTime{}; };
			let sc = UA_Client_HistoryRead_raw( _client, &node, onPage, ticks(request.Start), ticks(request.End), UA_STRING_NULL, request.Bounds, request.Limit, request.Timestamps, &context );
			return { sc, move(context.Values) };
		}
		Ω Read( UA_UInt32 id, Request request )ι->Reading{ return Read( Node(id), move(request) ); }
		//The backend's callback itself, for a session of user's:  the result's status.
		Ω Callback( UA_UInt32 id, UAAccess::SessionContext* session, TimePoint start, TimePoint end )ι->UA_StatusCode{
			let node = Node( id );
			UA_HistoryReadValueId read; UA_HistoryReadValueId_init( &read );
			read.nodeId = node;
			UA_ReadRawModifiedDetails details; UA_ReadRawModifiedDetails_init( &details );
			details.startTime = UADateTime{ start }.UA();
			details.endTime = UADateTime{ end }.UA();
			UA_HistoryReadResult result; UA_HistoryReadResult_init( &result );
			UA_HistoryData data; UA_HistoryData_init( &data );
			UA_HistoryData* p{ &data };
			History().ReadRaw( GetUAServer(), nullptr, session, details, UA_TIMESTAMPSTORETURN_BOTH, false, {&read, 1}, &result, &p );
			let status = result.statusCode;
			UA_HistoryData_clear( &data );
			UA_HistoryReadResult_clear( &result );
			return status;
		}
		Ω Property( const NodeId& parent, std::initializer_list<sv> path )ε->NodeId{
			vector<UA_QualifiedName> names;
			for( let name : path )
				names.push_back( {0, {name.size(), (UA_Byte*)name.data()}} );
			auto found = UA_Server_browseSimplifiedBrowsePath( GetUAServer().Ptr(), parent, names.size(), names.data() );
			let good = found.statusCode==UA_STATUSCODE_GOOD && found.targetsSize;
			NodeId y{ good ? found.targets[0].targetId.nodeId : UA_NODEID_NULL };
			UA_BrowsePathResult_clear( &found );
			THROW_IF( !good, "'{}' has no '{}'.", parent.ToString(), Str::Join(path, "/") );
			return y;
		}
		Ω Variant( const NodeId& node )ι->Value{
			UA_Variant v; UA_Variant_init( &v );
			if( let sc = UA_Server_readValue(GetUAServer().Ptr(), node, &v) )
				return Value{ (StatusCode)sc };
			return Value{ move(v) };
		}
		template<class T> Ω Setting( UA_UInt32 variable, sv name )ε->T{ return Variant( Property(Node(variable), {"HA Configuration", name}) ).template AsNumber<T>(); }

		//Seconds into the minute before the server started:  before the value each node took at its start, and, unless the
		//run crosses midnight, on its day.
		Ω At( uint second )ι->TimePoint{ return _base+seconds{ second }; }
		static inline UA_Client* _client{};
		static inline NsIndex _ns{};
		static inline TimePoint _base{};
		constexpr static UA_UInt32 Status1{ 6011 }, Rpm1{ 6012 }, Status2{ 6021 }, Rpm2{ 6022 }, Rpm3{ 6032 }, Rpm4{ 6042 }, RpmManual{ 6054 };
	};

	//What the nodeset marks is what is historized, each node under its HA Configuration's thresholds:  every deviation
	//format, a MinTimeInterval and a heartbeat.
	TEST_F( HistoryTests, HistorizesWhatTheNodesetMarks ){
		ASSERT_TRUE( History().Enabled() );
		for( let id : {Status1, Rpm1, Rpm2, Rpm3, Rpm4, RpmManual} )
			EXPECT_TRUE( Index(id) ) << id;
		EXPECT_FALSE( Index(Status2) );
		EXPECT_FALSE( Index(1003) );//the type's own motorRpm.

		let rpm1 = Thresholds( Rpm1 );
		EXPECT_EQ( rpm1.ExceptionDeviation, 5 );
		EXPECT_EQ( rpm1.DeviationFormat, UA_EXCEPTIONDEVIATIONFORMAT_ABSOLUTEVALUE );
		EXPECT_EQ( rpm1.MinTimeInterval, 1s );
		EXPECT_EQ( rpm1.MaxTimeInterval, Duration::zero() );
		EXPECT_FALSE( rpm1.Stepped );
		let rpm2 = Thresholds( Rpm2 );
		EXPECT_EQ( rpm2.ExceptionDeviation, 1 );
		EXPECT_EQ( rpm2.DeviationFormat, UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFVALUE );
		EXPECT_EQ( rpm2.MinTimeInterval, Duration::zero() );
		EXPECT_EQ( rpm2.MaxTimeInterval, 1min );
		let rpm3 = Thresholds( Rpm3 );
		EXPECT_EQ( rpm3.DeviationFormat, UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFRANGE );
		ASSERT_TRUE( rpm3.Range );
		EXPECT_EQ( rpm3.Range->High, 1500 );//its InstrumentRange, not its EURange.
		EXPECT_TRUE( rpm3.Stepped );
		let rpm4 = Thresholds( Rpm4 );
		EXPECT_EQ( rpm4.ExceptionDeviation, 0.5 );
		EXPECT_EQ( rpm4.DeviationFormat, UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFEURANGE );
		ASSERT_TRUE( rpm4.Range );
		EXPECT_EQ( rpm4.Range->Low, 0 );
		EXPECT_EQ( rpm4.Range->High, 3000 );
		EXPECT_EQ( Thresholds(RpmManual).ExceptionDeviation, 10 );
		let status = Thresholds( Status1 );
		EXPECT_FALSE( status.ExceptionDeviation );//every change.
		EXPECT_TRUE( status.Stepped );
	}

	//What a client needs to find a node's history and the nodeset left out:  the history bit, and the HA Configuration
	//with its defaults.
	TEST_F( HistoryTests, PublishesWhatTheNodesetLeavesOut ){
		auto ua = GetUAServer().Ptr();
		for( let id : {Status1, Rpm1, Rpm2, Rpm3, Rpm4, RpmManual} ){
			UA_Byte level{};
			ASSERT_EQ( UA_Server_readAccessLevel(ua, Node(id), &level), UA_STATUSCODE_GOOD ) << id;
			EXPECT_EQ( level, UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE | UA_ACCESSLEVELMASK_HISTORYREAD ) << id;
			UA_Boolean historizing{};
			EXPECT_EQ( UA_Server_readHistorizing(ua, Node(id), &historizing), UA_STATUSCODE_GOOD ) << id;
			EXPECT_TRUE( historizing ) << id;//the loader keeps the attribute.
			EXPECT_TRUE( Setting<bool>(id, "ServerTimestampSupported") ) << id;
			let today = UADateTime{ floor<days>(Clock::now()) }.UA();//no file yet, and nothing older than today.
			EXPECT_EQ( Variant(Property(Node(id), {"HA Configuration", "StartOfArchive"})).Get<UA_DateTime>(0), today ) << id;
			EXPECT_EQ( Variant(Property(Node(id), {"HA Configuration", "StartOfOnlineArchive"})).Get<UA_DateTime>(0), today ) << id;
		}
		UA_Byte level{};
		ASSERT_EQ( UA_Server_readAccessLevel(ua, Node(Status2), &level), UA_STATUSCODE_GOOD );
		EXPECT_EQ( level, UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE );

		//pump1.status has no HA Configuration in the nodeset.
		EXPECT_TRUE( Setting<bool>(Status1, "Stepped") );
		EXPECT_EQ( Setting<double>(Status1, "MinTimeInterval"), 0 );
		EXPECT_EQ( Setting<double>(Status1, "MaxTimeInterval"), 0 );
		EXPECT_THROW( Property(Node(Status1), {"HA Configuration", "ExceptionDeviation"}), Exception );//none stores every change.
		//The nodeset's are kept, and what it leaves out of one is added.
		EXPECT_FALSE( Setting<bool>(Rpm1, "Stepped") );
		EXPECT_EQ( Setting<double>(Rpm1, "MinTimeInterval"), 1000 );
		EXPECT_EQ( Setting<double>(Rpm1, "MaxTimeInterval"), 0 );
		EXPECT_TRUE( Setting<bool>(Rpm3, "Stepped") );
		EXPECT_EQ( Variant(Property(Node(RpmManual), {"HA Configuration", "ExceptionDeviationFormat"})).Get<UA_Int32>(0), UA_EXCEPTIONDEVIATIONFORMAT_ABSOLUTEVALUE );
	}

	//Only what is served is claimed:  raw reads, at most readLimit values a call.
	TEST_F( HistoryTests, PublishesItsCapabilities ){
		let capability = []( UA_UInt32 id ){ return Variant( NodeId{0, id} ); };
		EXPECT_TRUE( capability(UA_NS0ID_HISTORYSERVERCAPABILITIES_ACCESSHISTORYDATACAPABILITY).Get<UA_Boolean>(0) );
		EXPECT_EQ( capability(UA_NS0ID_HISTORYSERVERCAPABILITIES_MAXRETURNDATAVALUES).Get<UA_UInt32>(0), History().ReadLimit() );
		EXPECT_EQ( History().ReadLimit(), 10'000u );
		for( let id : {UA_NS0ID_HISTORYSERVERCAPABILITIES_ACCESSHISTORYEVENTSCAPABILITY, UA_NS0ID_HISTORYSERVERCAPABILITIES_INSERTDATACAPABILITY, UA_NS0ID_HISTORYSERVERCAPABILITIES_REPLACEDATACAPABILITY,
			UA_NS0ID_HISTORYSERVERCAPABILITIES_UPDATEDATACAPABILITY, UA_NS0ID_HISTORYSERVERCAPABILITIES_DELETERAWCAPABILITY, UA_NS0ID_HISTORYSERVERCAPABILITIES_DELETEATTIMECAPABILITY} )
			EXPECT_FALSE( capability(id).Get<UA_Boolean>(0) ) << id;
	}

	//setValue to HistoryRead:  what passes the node's band is stored with both timestamps, and read back over Part 11's
	//range, which holds its start and not its end, from the buffer and then from the day's file.
	TEST_F( HistoryTests, ReadsRawHistory ){
		Write( Rpm4, 100, At(0) );
		Write( Rpm4, 200, At(2) );
		Write( Rpm4, 300, At(4) );
		Write( Rpm4, 310, At(6) );//inside 0.5% of the EURange, 15 rpm, of the last stored.
		Write( Rpm4, 400, At(8) );
		for( let flushed : {false, true} ){
			SCOPED_TRACE( flushed ? "from the file" : "from the buffer" );
			if( flushed )
				ASSERT_TRUE( BlockAny(History().Group()->Flush()) );
			let forward = Read( Rpm4, {.Start=At(0), .End=At(10)} );
			EXPECT_TRUE( UA_StatusCode_isGood(forward.Status) ) << UA_StatusCode_name( forward.Status );
			let values = all( forward.Values );
			ASSERT_EQ( doubles(values), (vector<double>{100, 200, 300, 400}) );
			EXPECT_EQ( values[1].sourceTimestamp, UADateTime{At(2)}.UA() );
			EXPECT_TRUE( values[1].hasServerTimestamp );//stamped as the write arrived:  the server is this one.
			EXPECT_NEAR( (double)values[1].serverTimestamp, (double)UA_DateTime_now(), (double)UA_DATETIME_SEC*120 );
			EXPECT_FALSE( values[1].hasStatus );

			EXPECT_EQ( doubles(all(Read(Rpm4, {.Start=At(0), .End=At(8)}).Values)), (vector<double>{100, 200, 300}) );
			EXPECT_EQ( doubles(all(Read(Rpm4, {.Start=At(8), .End=At(0)}).Values)), (vector<double>{400, 300, 200}) );
			EXPECT_EQ( doubles(all(Read(Rpm4, {.Start=At(1), .End=At(7), .Bounds=true}).Values)), (vector<double>{100, 200, 300, 400}) );
			EXPECT_EQ( doubles(all(Read(Rpm4, {.Start=At(2), .End=At(2)}).Values)), (vector<double>{200}) );

			let paged = Read( Rpm4, {.Start=At(0), .End=At(10), .Limit=3} );
			EXPECT_EQ( doubles(all(paged.Values)), (vector<double>{100, 200, 300, 400}) );
			ASSERT_GE( paged.Values.size(), 2u );
			EXPECT_EQ( paged.Values[0].size(), 3u );
			//The last two, with the continuation point released, not followed.
			let last = Read( Rpm4, {.End=At(10), .Limit=2, .MaxPages=1} );
			EXPECT_TRUE( UA_StatusCode_isGood(last.Status) ) << UA_StatusCode_name( last.Status );
			EXPECT_EQ( doubles(all(last.Values)), (vector<double>{400, 300}) );

			let sourced = all( Read(Rpm4, {.Start=At(0), .End=At(10), .Timestamps=UA_TIMESTAMPSTORETURN_SOURCE}).Values );
			ASSERT_EQ( sourced.size(), 4u );
			EXPECT_TRUE( sourced[0].hasSourceTimestamp );
			EXPECT_FALSE( sourced[0].hasServerTimestamp );
		}
		let nothing = Read( Rpm4, {.Start=At(100), .End=At(200)} );
		EXPECT_EQ( nothing.Status, UA_STATUSCODE_GOODNODATA );
		EXPECT_TRUE( all(nothing.Values).empty() );
		EXPECT_GT( History().Reads().Count, 0u );
		EXPECT_GT( History().Collections().Count, 0u );
	}

	//Each call reads one day's file, so a read over two days comes in two pages, though neither is full.  Today's holds
	//the 100 and, after it, the 0 the node took at the start.
	TEST_F( HistoryTests, ReadsADayACall ){
		let tomorrow = floor<days>( Clock::now() )+days{ 1 };
		Write( Rpm3, 100, At(20) );
		Write( Rpm3, 200, tomorrow+10s );
		Write( Rpm3, 300, tomorrow+20s );
		let forward = Read( Rpm3, {.Start=At(20), .End=tomorrow+1min} );
		EXPECT_TRUE( UA_StatusCode_isGood(forward.Status) ) << UA_StatusCode_name( forward.Status );
		ASSERT_EQ( forward.Values.size(), 2u );
		EXPECT_EQ( doubles(forward.Values[0]), (vector<double>{100, 0}) );
		EXPECT_EQ( doubles(forward.Values[1]), (vector<double>{200, 300}) );
		let back = Read( Rpm3, {.Start=tomorrow+1min, .End=At(19)} );
		EXPECT_TRUE( UA_StatusCode_isGood(back.Status) ) << UA_StatusCode_name( back.Status );
		ASSERT_EQ( back.Values.size(), 2u );
		EXPECT_EQ( doubles(back.Values[0]), (vector<double>{300, 200}) );
		EXPECT_EQ( doubles(back.Values[1]), (vector<double>{0, 100}) );
	}

	TEST_F( HistoryTests, RefusesWhatItCannotServe ){
		EXPECT_EQ( Read(Status2, {.Start=At(0), .End=At(10)}).Status, UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED );//not historized.
		EXPECT_EQ( Read(Node(999'999), {.Start=At(0), .End=At(10)}).Status, UA_STATUSCODE_BADNODEIDUNKNOWN );
		EXPECT_EQ( Read(Rpm4, {.Start=At(0)}).Status, UA_STATUSCODE_BADHISTORYOPERATIONINVALID );//two of start, end and a count bound a read.
		EXPECT_EQ( Read(Rpm4, {.Start=At(0), .End=At(10), .Timestamps=UA_TIMESTAMPSTORETURN_NEITHER}).Status, UA_STATUSCODE_BADINVALIDTIMESTAMPARGUMENT );
		let node = Node( Rpm4 );
		let modified = UA_Client_HistoryRead_modified( _client, &node, []( UA_Client*, const UA_NodeId*, UA_Boolean, const UA_ExtensionObject*, void* )->UA_Boolean{ return true; },
			UADateTime{At(0)}.UA(), UADateTime{At(10)}.UA(), UA_STRING_NULL, false, 0, UA_TIMESTAMPSTORETURN_BOTH, nullptr );
		EXPECT_EQ( modified, UA_STATUSCODE_BADNOTSUPPORTED );//with the edits, #210.
	}

	//Read on the node is the right, asked of the session's user for each node:  never the collector's.
	TEST_F( HistoryTests, ReadNeedsReadOnTheNode ){
		auto& authorizer = static_cast<OpcAuthorize&>( *GetSchema().Authorizer );
		let status = [&]( UserPK user ){
			UAAccess::SessionContext session{ "", TimePoint::max(), 0, user };
			return Callback( Rpm4, &session, At(0), At(10) );
		};
		EXPECT_TRUE( UA_StatusCode_isGood(status(AppClient()->UserPK())) );
		const UserPK stranger{ 0x7FFF'FFF0 };
		let mayRead = !empty( authorizer.NodeRights(Node(Rpm4), stranger) & Access::ERights::Read );//every node is open until a resource is configured.
		EXPECT_EQ( UA_StatusCode_isGood(status(stranger)), mayRead );
		if( !mayRead )
			EXPECT_EQ( status(stranger), UA_STATUSCODE_BADUSERACCESSDENIED );
		EXPECT_EQ( Callback(Rpm4, nullptr, At(0), At(10)), UA_STATUSCODE_BADUSERACCESSDENIED );//no session.
	}

	//A start after a stop:  the files give each node its index back, a node the nodesets no longer historize is removed,
	//and each node's value now is its first after the break.  Last:  it replaces the suite's server.
	TEST_F( HistoryTests, RestartKeepsIndexesAndRemovesWhatIsNoLongerHistorized ){
		Disconnect();
		let rpm4 = Index( Rpm4 ), manual = Index( RpmManual );
		ASSERT_TRUE( rpm4 && manual );
		let wrote = floor<microseconds>( Clock::now() );
		Write( Rpm4, 500, wrote );
		Write( RpmManual, 77, wrote );

		std::ifstream in{ nodeset(), std::ios::binary };
		string xml{ std::istreambuf_iterator<char>{in}, {} };
		let marked = "ParentNodeId=\"ns=1;i=5005\" DataType=\"Double\" AccessLevel=\"3\" Historizing=\"true\""sv;
		let at = xml.find( marked );
		ASSERT_NE( at, string::npos );
		xml.erase( at+marked.size()-sizeof("Historizing=\"true\""), sizeof("Historizing=\"true\"") );
		let edited = fs::temp_directory_path()/"pumps.unhistorized.NodeSet2.xml";
		std::ofstream{ edited, std::ios::binary | std::ios::trunc } << xml;
		Start( edited );
		fs::remove( edited );

		EXPECT_EQ( Index(Rpm4), rpm4 );
		EXPECT_FALSE( Index(RpmManual) );
		bool removed{};
		for( let& record : History().Group()->Buffer() ){
			if( let p = get_if<Hist::NodeRemoved>(&record) )
				removed = removed || p->Index==*manual;
		}
		EXPECT_TRUE( removed );
		//What the stop wrote, then the gap it left, then the nodeset's value, which the server starts from.
		let page = History().Group()->Read( {.Nodes={*rpm4}, .Start=UADateTime{wrote}.UA(), .End=UA_DateTime_now()} );
		ASSERT_GE( page.Values.size(), 3u );
		EXPECT_EQ( page.Values[0].Value.value().double_value(), 500 );
		EXPECT_EQ( page.Values[1].Value.status(), UA_STATUSCODE_BADDATALOST );
		EXPECT_EQ( page.Values[2].Value.value().double_value(), 0 );
		EXPECT_EQ( Variant(Property(Node(Rpm4), {"HA Configuration", "StartOfArchive"})).Get<UA_DateTime>(0), UADateTime{floor<days>(Clock::now())}.UA() );
	}
}