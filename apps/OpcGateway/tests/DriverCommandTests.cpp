#include <open62541/client_highlevel_async.h>
#include <jde/fwk/process/execution.h>
#include <jde/tests/Capture.h>
#include "utils/DriverResults.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	using OpenProtocol::Controller;
	using OpenProtocol::Mid0018;
	using OpenProtocol::Mid0038;
	using OpenProtocol::Mid0050;

	//docs/open-protocol/plan.md Phase 8 (#470):  the Open Protocol driver's `Commands` (drivers/openProtocol/OpenProtocolDriver.h) and
	//the calls its host holds (drivers/DriverCalls.h) - the five methods called through the QL's `call`, as a web session's client
	//calls one under its issued token, against the library's test controller, with the right the host asks for them.
	struct DriverCommandTests : DriverResultFixture{
	protected:
		static constexpr StatusCode Good{ UA_STATUSCODE_GOOD }, Denied{ UA_STATUSCODE_BADUSERACCESSDENIED }, OutOfRange{ UA_STATUSCODE_BADOUTOFRANGE };
		//`_Op.Revisions` once the connection's seven requests are answered:  nothing of the driver's is outstanding.
		static constexpr sv Subscribed{ "0001:6 0015:3 0035:6 0052:2 0061:7 0071:3 0080:1" };
		Ω WaitRevisions( const DriverRow& row, sv text )ι->bool{ return WaitOp( row, "Revisions", [=]( let& v ){ return OpText( v )==text; } ); }
		Ω Method( sv name )ι->NodeId{ return DriverNode( Ƒ("Commands.{}", name) ); }
		//A method of the row's `Commands` through the QL's `call`:  the call's status, the server's whatever it is.
		Ω Call( const DriverRow& row, sv method, jarray args={} )ε->StatusCode{
			const jobject vars{ {"opc", row.Slug}, {"object", DriverNode("Commands").ToJson()}, {"method", Method(method).ToJson()}, {"args", move(args)} };
			return Socket().QuerySync( "call( opc: $opc, object: $object, method: $method, args: $args ){ statusCode }", vars ).as_object().at( "statusCode" ).to_number<StatusCode>();
		}
		//Until the session's user may, or may not, call the row's methods, as a method's UserExecutable has it:  a grant reaches
		//the gateway's authorizer after the mutation that made it returns.
		Ω WaitExecutable( const DriverRow& row, bool executable )ι->bool{
			return WaitUntil( [&]{
				try{
					let node = Socket().QuerySync( "node( opc: $opc, id: $id ){ userExecutable }", {{"opc", row.Slug}, {"id", Method("EnableTool").ToJson()}} ).as_object();
					return Json::FindBool( node, "userExecutable" )==executable;
				}
				catch( const std::exception& ){
					return false;
				}
			}, 15s );
		}
		//The messages of `T` the controller heard, from its frame `from` on.
		template<class T> Ω Heard( const Controller& station, uint from=0 )ι->vector<T>{
			vector<T> y;
			let frames = station.Frames();
			for( uint i=from; i<frames.size(); ++i ){
				if( frames[i].Head.Mid!=T::Id )
					continue;
				let message = OpenProtocol::Decode( frames[i].Frame );
				if( let heard = message ? get_if<T>(&message->Data) : nullptr )
					y.push_back( *heard );
			}
			return y;
		}
		//The commands the controller heard, by MID, from its frame `from` on.
		Ω Commands( const Controller& station, uint from=0 )ι->vector<uint16_t>{
			vector<uint16_t> y;
			let frames = station.Frames();
			for( uint i=from; i<frames.size(); ++i ){
				if( let mid = frames[i].Head.Mid; mid==18 || mid==38 || mid==42 || mid==43 || mid==50 )
					y.push_back( mid );
			}
			return y;
		}
		Ω Text( const Value& v )ι->string{ return v.hasValue && !v.IsEmpty() ? serialize( v.ValueJson(nullptr) ) : "no value"; }
		//Until the row's `path` reads Good with `json`, as a web session's client reads it.
		Ω WaitValue( const DriverRow& row, sv path, sv json )ι->bool{
			return WaitUntil( [&]{
				try{
					let v = ReadValue( BlockAwait(UAClient::GetClient(row.Slug, SessionCredential())), DriverNode(path) );
					return !v.status && Text( v )==json;
				}
				catch( const std::exception& ){
					return false;
				}
			}, 15s );
		}
		//A call's line starts so, and ends with its answer (DriverCalls).
		Ω Line( sv slug, sv call )ι->string{ return Ƒ( "Call by '{}' of {} on '{}'", AppClient()->Acl()->UserName(User()), call, slug ); }
		//What follows that start in each line a call left, from `lines`' start.
		Ω Lines( const Logging::Capture& lines, sv slug, sv call )ι->vector<string>{
			vector<string> y;
			let start = Line( slug, call );
			for( let& entry : lines.Find([&]( const Logging::Entry& e ){ return e.Message().starts_with( start ); }) )
				y.push_back( entry.Message().substr(start.size()) );
			return y;
		}
		Ω WaitLine( const Logging::Capture& lines, sv slug, sv call, sv answer )ι->bool{
			return WaitUntil( [&]{ return std::ranges::contains( Lines(lines, slug, call), answer ); }, 15s );
		}
		//A key of the suite's /gateway/drivers/openProtocol block at `value` for the rows made until this goes, when it is the
		//shipped one again:  a driver reads the block as it is made.
		struct BlockSetting final{
			BlockSetting( sv name, jvalue value, jvalue shipped )ε:_path{ Ƒ("/gateway/drivers/openProtocol/{}", name) }, _shipped{ move(shipped) }{ Settings::Set( _path, move(value) ); }
			~BlockSetting(){ try{ Settings::Set( _path, _shipped ); }catch( const std::exception& ){} }
			BlockSetting( const BlockSetting& )=delete;
		private:
			string _path;
			jvalue _shipped;
		};
		//A session on the row's server of the suite's own, by the process's pair and this program's hub session, the suite's user:
		//one a case closes when it likes, which the gateway's clients are not.
		Ω Client( const DriverRow& row )ε->up<UATestClient>{
			let host = DriverHost::Find( row.Slug );
			THROW_IF( !host, "'{}' has no server.", row.Slug );
			auto y = mu<UATestClient>( Ƒ("opc.tcp://127.0.0.1:{}", host->Port()), Ssl(), Issued(), UA_MESSAGESECURITYMODE_SIGNANDENCRYPT );
			y->ConnectOrThrow();
			return y;
		}
		//A call sent and not waited for.
		Ω Send( UATestClient& client, sv method, const UA_Variant* input=nullptr )ε->void{
			UAε( UA_Client_call_async(client, DriverNode("Commands"), Method(method), input ? 1 : 0, input, []( UA_Client*, void*, UA_UInt32, UA_CallResponse* ){}, nullptr, nullptr) );
		}
		Ω Argument( UA_UInt16& value )ι->UA_Variant{
			UA_Variant y;
			UA_Variant_setScalar( &y, &value, &UA_TYPES[UA_TYPES_UINT16] );
			return y;
		}
	};

	//`Commands` holds the five methods, each with the argument it takes, and each sends its MID and answers Good on MID 0005:  a
	//parameter set by MID 0018, a job by MID 0038 at revision 2, the tool disabled and enabled by MIDs 0042 and 0043 and a VIN by
	//MID 0050.  What the controller then reports lands in `Selected`, `_Op.Revisions` names each command's revision, and every
	//call is a line with the user, the method, its argument and the answer.
	TEST_F( DriverCommandTests, EachCommandSendsItsMid ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-commands", NewYork };
		ASSERT_TRUE( WaitRevisions(row, Subscribed) ) << OpText( Op(row, "Revisions") );

		vector<string> methods;
		let commands = Socket().QuerySync( "node( opc: $opc, id: $id ){ children{ name nodeClass } }", {{"opc", row.Slug}, {"id", DriverNode("Commands").ToJson()}} ).as_object();
		for( let& child : Json::AsArray(commands, "children") ){
			methods.push_back( Json::AsString(child.as_object(), "name") );
			EXPECT_EQ( Json::FindNumber<uint>(child.as_object(), "nodeClass"), UA_NODECLASS_METHOD ) << methods.back();
		}
		std::ranges::sort( methods );
		EXPECT_EQ( methods, (vector<string>{"DisableTool", "DownloadVin", "EnableTool", "SelectJob", "SelectPset"}) );
		let arguments = [&]( sv method ){
			return Socket().QuerySync( "method( opc: $opc, id: $id ){ inputArguments{ name dataType valueRank } }", {{"opc", row.Slug}, {"id", Method(method).ToJson()}} ).as_object().at( "inputArguments" );
		};
		let argument = []( sv name, UA_UInt32 type ){ return jvalue{ jarray{jobject{{"name", name}, {"dataType", NodeId{type}.ToJson()}, {"valueRank", -1}}} }; };
		EXPECT_EQ( arguments("SelectPset"), argument("pset", UA_NS0ID_UINT16) );
		EXPECT_EQ( arguments("SelectJob"), argument("job", UA_NS0ID_UINT16) );
		EXPECT_EQ( arguments("DownloadVin"), argument("vin", UA_NS0ID_STRING) );
		EXPECT_EQ( arguments("DisableTool"), jvalue{jarray{}} );
		EXPECT_EQ( arguments("EnableTool"), jvalue{jarray{}} );

		const ResultGroup group{ row, "driver-commands", {{"Result.Torque", {}}} };
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitExecutable(row, true) ) << "the grant reaches the gateway's authorizer";
		let host = DriverHost::Find( row.Slug );
		ASSERT_TRUE( host );
		Logging::Capture lines{ ELogLevel::Information };
		let from = station.Frames().size();

		EXPECT_EQ( Call(row, "SelectPset", {7}), Good );
		let psets = Heard<Mid0018>( station, from );
		ASSERT_EQ( psets.size(), 1u );
		EXPECT_EQ( psets[0].Pset, optional<uint16_t>{7} );
		EXPECT_TRUE( WaitValue(row, "Selected.Pset", "7") ) << "MID 0015, as the controller reports the set it took";

		EXPECT_EQ( Call(row, "SelectJob", {12}), Good );
		let jobs = Heard<Mid0038>( station, from );
		ASSERT_EQ( jobs.size(), 1u );
		EXPECT_EQ( jobs[0].JobId, optional<uint16_t>{12} );
		EXPECT_EQ( RevisionsOf(station, 38, from), vector<uint16_t>{2} ) << "four digits";
		EXPECT_TRUE( WaitValue(row, "Selected.Job", "12") );

		EXPECT_TRUE( station.ToolEnabled() );
		EXPECT_EQ( Call(row, "DisableTool"), Good );
		EXPECT_FALSE( station.ToolEnabled() );
		EXPECT_EQ( Call(row, "EnableTool"), Good );
		EXPECT_TRUE( station.ToolEnabled() );

		EXPECT_EQ( Call(row, "DownloadVin", {"VIN0123456789"}), Good );
		let vins = Heard<Mid0050>( station, from );
		ASSERT_EQ( vins.size(), 1u );
		EXPECT_EQ( vins[0].Vin, "VIN0123456789" );
		EXPECT_TRUE( WaitValue(row, "Selected.Vin", "\"VIN0123456789\"") );

		EXPECT_EQ( Commands(station, from), (vector<uint16_t>{18, 38, 42, 43, 50}) ) << "one frame a call, each at its first asking";
		EXPECT_TRUE( WaitRevisions(row, "0001:6 0015:3 0018:1 0035:6 0038:2 0042:1 0043:1 0050:1 0052:2 0061:7 0071:3 0080:1") ) << OpText( Op(row, "Revisions") );
		for( let call : {"SelectPset( 7 )", "SelectJob( 12 )", "DisableTool()", "EnableTool()", "DownloadVin( \"VIN0123456789\" )"} )
			EXPECT_EQ( Lines(lines, row.Slug, call), vector<string>{":  Good."} ) << call;
		EXPECT_EQ( host->Calls(), 0u ) << "each answered";
	}

	//A value no revision carries is refused Bad_OutOfRange by the method itself, with nothing sent:  a parameter set past 999, a
	//job past 9999 and a VIN past 25 characters, each to its last value taken.  A VIN's control character is escaped in its line,
	//and a VIN past 50 bytes is refused by its length, the line naming the length alone.  A controller with MID 0038 at revision 1
	//alone is asked at 2 and then at 1, two digits;  the revision settled, a job past 99 is refused with nothing sent at all.
	TEST_F( DriverCommandTests, AValuePastItsFieldIsOutOfRange ){
		auto script = Station();
		script.Revisions[38] = 1;
		Controller station{ *Executor(), ControllerPort(), move(script) };
		const DriverRow row{ "driver-command-range" };
		ASSERT_TRUE( WaitRevisions(row, Subscribed) ) << OpText( Op(row, "Revisions") );
		const ResultGroup group{ row, "driver-command-range", {{"Result.Torque", {}}} };
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitExecutable(row, true) );
		Logging::Capture lines{ ELogLevel::Information };
		let from = station.Frames().size();

		EXPECT_EQ( Call(row, "SelectPset", {1000}), OutOfRange );
		EXPECT_EQ( Call(row, "SelectJob", {10000}), OutOfRange );
		EXPECT_EQ( Call(row, "DownloadVin", {jvalue{string(26, 'V')}}), OutOfRange );
		EXPECT_EQ( Call(row, "DownloadVin", {jvalue{"VIN\n0"}}), OutOfRange ) << "a control character, which the codec refuses";
		EXPECT_EQ( Call(row, "DownloadVin", {jvalue{string(51, 'V')}}), OutOfRange );
		EXPECT_EQ( Commands(station, from), vector<uint16_t>{} ) << "nothing sent";
		EXPECT_EQ( Lines(lines, row.Slug, "SelectPset( 1000 )"), vector<string>{":  BadOutOfRange - MID 0018 revision 1, byte 21:  1000 does not fit 3 digits."} );
		EXPECT_EQ( Lines(lines, row.Slug, "DownloadVin( \"VIN\\n0\" )").size(), 1u ) << "escaped in its line";
		EXPECT_EQ( Lines(lines, row.Slug, "DownloadVin( 51 bytes )"), vector<string>{":  BadOutOfRange - A VIN is at most 25 characters, 50 bytes."} );
		let raw = lines.Find( []( const Logging::Entry& e ){ let m = e.Message(); return m.contains( "DownloadVin" ) && (m.contains( '\n' ) || m.contains( string(51, 'V') )); } );
		EXPECT_TRUE( raw.empty() ) << "no line carries the newline or the long VIN:  " << (raw.empty() ? string{} : raw.front().Message());

		EXPECT_EQ( Call(row, "SelectPset", {999}), Good );
		EXPECT_EQ( Call(row, "DownloadVin", {jvalue{string(25, 'V')}}), Good );
		EXPECT_EQ( Commands(station, from), (vector<uint16_t>{18, 50}) );

		let unsettled = station.Frames().size();
		EXPECT_EQ( Call(row, "SelectJob", {100}), OutOfRange ) << "asked at 2, which the controller lacks, and no revision 1 carries it";
		EXPECT_EQ( RevisionsOf(station, 38, unsettled), vector<uint16_t>{2} ) << "nothing sent at 1";
		let settling = station.Frames().size();
		EXPECT_EQ( Call(row, "SelectJob", {12}), Good );
		EXPECT_EQ( RevisionsOf(station, 38, settling), (vector<uint16_t>{2, 1}) ) << "asked at 2, then at 1";
		let jobs = Heard<Mid0038>( station, settling );
		ASSERT_EQ( jobs.size(), 2u );
		EXPECT_EQ( jobs[1].JobId, optional<uint16_t>{12} );
		EXPECT_TRUE( WaitOp(row, "Revisions", []( let& v ){ return OpText( v ).contains( "0038:1" ); }) ) << OpText( Op(row, "Revisions") );

		let settled = station.Frames().size();
		EXPECT_EQ( Call(row, "SelectJob", {100}), OutOfRange );
		EXPECT_EQ( Commands(station, settled), vector<uint16_t>{} ) << "the settled range:  nothing sent";
		EXPECT_EQ( Call(row, "SelectJob", {99}), Good );
		EXPECT_EQ( RevisionsOf(station, 38, settled), vector<uint16_t>{1} );
	}

	//MID 0004 is Bad_InvalidState, with its MID, its code and the code's name in `_Op.LastError` and the call's line:  a
	//controller whose clients are Viewers refuses each command with code 92.  The link stands, and a command it takes is Good.
	TEST_F( DriverCommandTests, ARefusalIsInvalidState ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-command-refused" };
		ASSERT_TRUE( WaitRevisions(row, Subscribed) ) << OpText( Op(row, "Revisions") );
		const ResultGroup group{ row, "driver-command-refused", {{"Result.Torque", {}}} };
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitExecutable(row, true) );
		Logging::Capture lines{ ELogLevel::Information };
		station.Edit( []( Controller::Script& script ){ script.Viewer = true; } );
		struct Refused{ sv Method; jarray Args; uint16_t Mid; sv Call; };
		for( let& c : {Refused{"SelectPset", {7}, 18, "SelectPset( 7 )"}, Refused{"SelectJob", {12}, 38, "SelectJob( 12 )"}, Refused{"DisableTool", {}, 42, "DisableTool()"},
			Refused{"EnableTool", {}, 43, "EnableTool()"}, Refused{"DownloadVin", {"VIN7"}, 50, "DownloadVin( \"VIN7\" )"}} ){
			EXPECT_EQ( Call(row, c.Method, c.Args), UA_STATUSCODE_BADINVALIDSTATE ) << c.Method;
			let refusal = Ƒ( "MID 0004 to MID {:04}:  code 92, Open protocol commands disabled.", c.Mid );
			EXPECT_EQ( OpText(Op(row, "LastError")), refusal ) << c.Method;
			EXPECT_EQ( Lines(lines, row.Slug, c.Call), vector<string>{Ƒ(":  BadInvalidState - {}", refusal)} ) << c.Method;
		}
		EXPECT_TRUE( station.ToolEnabled() ) << "refused";
		EXPECT_EQ( OpFlag(Op(row, "Connected")), optional<bool>{true} ) << "a refusal is no fault of the link's";
		station.Edit( []( Controller::Script& script ){ script.Viewer = false; } );
		EXPECT_EQ( Call(row, "DisableTool"), Good );
		EXPECT_FALSE( station.ToolEnabled() );
	}

	//A controller silent for replyTimeout answers the call Bad_Timeout, and has lost its link by it;  while the link is down a
	//call is Bad_NoCommunication, with nothing sent, and a value out of range Bad_OutOfRange still, measured before the link is
	//asked for;  the link back, a call is Good.
	TEST_F( DriverCommandTests, ASilentControllerIsTimeoutAndADownLinkNoCommunication ){
		const BlockSetting replyTimeout{ "replyTimeout", "PT1S", "PT10S" };
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-command-silent" };
		ASSERT_TRUE( WaitRevisions(row, Subscribed) ) << OpText( Op(row, "Revisions") );
		const ResultGroup group{ row, "driver-command-silent", {{"Result.Torque", {}}} };
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitExecutable(row, true) );
		Logging::Capture lines{ ELogLevel::Information };

		station.Silence();
		let from = station.Frames().size();
		EXPECT_EQ( Call(row, "SelectPset", {7}), UA_STATUSCODE_BADTIMEOUT );
		EXPECT_EQ( Heard<Mid0018>(station, from).size(), 1u ) << "sent, and never answered";
		EXPECT_EQ( Lines(lines, row.Slug, "SelectPset( 7 )"), vector<string>{":  BadTimeout - MID 0018 had no answer within 1000 ms."} );
		ASSERT_TRUE( WaitConnected(row, false) ) << "the silence is the link's loss";

		ASSERT_TRUE( Sever(station, row) );//refused at each MID 0001 from here, where the silence would hold each attempt for replyTimeout.
		station.Silence( false );
		let down = station.Frames().size();
		EXPECT_EQ( Call(row, "EnableTool"), UA_STATUSCODE_BADNOCOMMUNICATION );
		EXPECT_EQ( Commands(station, down), vector<uint16_t>{} ) << "nothing sent";
		EXPECT_EQ( Lines(lines, row.Slug, "EnableTool()"), vector<string>{":  BadNoCommunication - The link is down."} );
		EXPECT_EQ( Call(row, "SelectPset", {1000}), OutOfRange ) << "the method's own refusal, whatever the link";

		Mend( station );
		ASSERT_TRUE( WaitConnected(row) );
		EXPECT_EQ( Call(row, "EnableTool"), Good );
	}

	//The right comes first, from the Call service (DriverHost.h):  a row no group holds refuses its commands;  so does a group
	//over its `Result.Torque` while nothing enforces the group's row;  the row enforced, the suite's user, granted everything
	//on it, commands, though no method of the server's own;  and under a row of the group's own that grants Read alone is
	//refused again, a second group on the connection with no member giving it nothing.  Each refusal is Bad_UserAccessDenied
	//with nothing sent, a method's UserExecutable says so beforehand, and why is a line in the log.
	TEST_F( DriverCommandTests, ACallNeedsExecuteOnAnEnforcedGroup ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-command-rights" };
		ASSERT_TRUE( WaitRevisions(row, Subscribed) ) << OpText( Op(row, "Revisions") );
		Logging::Capture lines{ ELogLevel::Information };
		let from = station.Frames().size();
		let refusals = [&]( sv why, const NodeId& method=Method("EnableTool") ){
			return lines.Find( [&, line=Ƒ("Call by '{}' of {} on '{}' refused, Bad_UserAccessDenied:  {}", AppClient()->Acl()->UserName(User()), method.ToString(), row.Slug, why)]( const Logging::Entry& e ){
				return e.Message()==line;
			} ).size();
		};

		ASSERT_TRUE( WaitExecutable(row, false) );
		EXPECT_EQ( Call(row, "EnableTool"), Denied ) << "no group holds a variable of the row";
		EXPECT_EQ( refusals("No group holds a variable of the connection, so nothing grants Execute on it."), 1u );

		const ResultGroup group{ row, "driver-command-rights", {{"Result.Torque", {}}} };
		EXPECT_EQ( Call(row, "EnableTool"), Denied ) << "nothing enforces the group's row";
		EXPECT_EQ( refusals(Ƒ("'Execute' access to 'histGroups[{}]' needs a grant on an enforced resource, and nothing enforces it.", group.Host->Guid())), 1u );
		ASSERT_TRUE( WaitExecutable(row, false) );

		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitExecutable(row, true) ) << "the grant reaches the gateway's authorizer";
		EXPECT_EQ( Call(row, "EnableTool"), Good );
		EXPECT_EQ( Lines(lines, row.Slug, "EnableTool()"), vector<string>{":  Good."} ) << "the audit line";
		const NodeId monitored{ UA_NS0ID_SERVER_GETMONITOREDITEMS };
		const jobject own{ {"opc", row.Slug}, {"object", NodeId{UA_NS0ID_SERVER}.ToJson()}, {"method", monitored.ToJson()}, {"args", jarray{4000000000}} };//a subscription no session has:  called, it would answer Bad_SubscriptionIdInvalid.
		EXPECT_EQ( Socket().QuerySync("call( opc: $opc, object: $object, method: $method, args: $args ){ statusCode }", own).as_object().at("statusCode").to_number<StatusCode>(), Denied ) << "a method of the server's own";
		EXPECT_EQ( refusals("It is no method of the connection's driver.", monitored), 1u );

		ScopeHistGroup( group.Id, Access::ERights::Read );
		absl::Cleanup unscope = [&]{ try{ UnscopeHistGroup( group.Id ); }catch( const std::exception& e ){ ADD_FAILURE() << "the group's resource wasn't deleted:  " << e.what(); } };
		ASSERT_TRUE( WaitExecutable(row, false) ) << "the group's own row reaches the gateway's authorizer";
		EXPECT_EQ( Call(row, "EnableTool"), Denied ) << "Read alone";
		let unheld = Ƒ( "User does not have 'Execute' access to 'histGroups[{}]'.", group.Host->Guid() );
		EXPECT_EQ( refusals(unheld), 1u );
		let spare = Id( Mutate("createHistGroup( name:$name, slug:$slug, serverConnectionId:$connection ){ id }", {{"name", "Commands spare"}, {"slug", "driver-command-spare"}, {"connection", row.Id}}) );
		absl::Cleanup purge = [&]{ try{ Mutate( "purgeHistGroup( id:$id )", {{"id", spare}} ); }catch( const std::exception& e ){ ADD_FAILURE() << "purgeHistGroup(" << spare << "): " << e.what(); } };
		EXPECT_EQ( Call(row, "EnableTool"), Denied ) << "a group with no member holds no variable of the row, whatever its row grants";
		EXPECT_EQ( refusals(unheld), 2u );

		EXPECT_EQ( Commands(station, from), vector<uint16_t>{43} ) << "the one call with the right:  a refused one sends nothing";
		EXPECT_EQ( Lines(lines, row.Slug, "EnableTool()").size(), 1u ) << "a refused call never reaches its method";
	}

	//The row's server holds a call callTimeout, open62541's asyncOperationTimeout:  past it the client is answered Bad_Timeout
	//and the host forgets the call.  The request went all the same, and the controller's answer, when it comes, is logged and
	//never set.
	TEST_F( DriverCommandTests, ACallPastCallTimeoutIsCancelled ){
		const BlockSetting callTimeout{ "callTimeout", "PT1S", "PT30S" };
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-command-timeout" };
		ASSERT_TRUE( WaitRevisions(row, Subscribed) ) << OpText( Op(row, "Revisions") );
		const ResultGroup group{ row, "driver-command-timeout", {{"Result.Torque", {}}} };
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitExecutable(row, true) );
		let host = DriverHost::Find( row.Slug );
		ASSERT_TRUE( host );
		Logging::Capture lines{ ELogLevel::Information };

		EXPECT_EQ( Call(row, "EnableTool"), Good ) << "inside callTimeout";//and the link busy just now:  no keep-alive's reply takes the delay.
		station.Delay( 3500ms );
		EXPECT_EQ( Call(row, "SelectPset", {7}), UA_STATUSCODE_BADTIMEOUT ) << "open62541's, at callTimeout";
		EXPECT_EQ( host->Calls(), 0u ) << "forgotten at the cancel";
		EXPECT_EQ( Lines(lines, row.Slug, "SelectPset( 7 )"), vector<string>{} ) << "no answer yet";
		EXPECT_TRUE( WaitValue(row, "Selected.Pset", "7") ) << "sent:  the controller took the set";
		EXPECT_TRUE( WaitLine(lines, row.Slug, "SelectPset( 7 )", " answered after open62541 cancelled it - a timeout, a client's Cancel, the queue's limit or the shutdown:  Good.") );
		EXPECT_EQ( Lines(lines, row.Slug, "SelectPset( 7 )").size(), 1u );
		EXPECT_EQ( OpFlag(Op(row, "Connected")), optional<bool>{true} );
		EXPECT_EQ( Call(row, "SelectPset", {8}), Good ) << "the next call is its own";
	}

	//A call open62541 cancels before the strand has it is not sent:  past the block's maxAsyncOperations the Call is answered
	//Bad_TooManyOperations once its method has returned, and the request reaches the strand through the server's thread only
	//after the service, so the strand finds the call cancelled, the executor free.  The call held before it is answered as ever.
	TEST_F( DriverCommandTests, ACallPastTheQueuesLimitIsNotSent ){
		const BlockSetting limit{ "maxAsyncOperations", 1, 0 };
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-command-limit" };
		ASSERT_TRUE( WaitRevisions(row, Subscribed) ) << OpText( Op(row, "Revisions") );
		const ResultGroup group{ row, "driver-command-limit", {{"Result.Torque", {}}} };
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitExecutable(row, true) );
		let host = DriverHost::Find( row.Slug );
		ASSERT_TRUE( host );
		Logging::Capture lines{ ELogLevel::Information };
		let from = station.Frames().size();
		UA_UInt16 pset{ 7 };
		let set = Argument( pset );
		auto client = Client( row );
		station.Delay( 2s );
		Send( *client, "SelectPset", &set );
		ASSERT_TRUE( WaitUntil([&]{ UA_Client_run_iterate( *client, 10 ); return host->Calls()==1 && Commands( station, from ).size()==1; }) ) << "held, and with the controller";
		size_t outputs{};
		UA_Variant* output{};
		EXPECT_EQ( UA_Client_call(*client, DriverNode("Commands"), Method("DisableTool"), 0, nullptr, &outputs, &output), UA_STATUSCODE_BADTOOMANYOPERATIONS );
		UA_Array_delete( output, outputs, &UA_TYPES[UA_TYPES_VARIANT] );
		EXPECT_EQ( host->Calls(), 1u ) << "the one before it";
		EXPECT_TRUE( WaitLine(lines, row.Slug, "DisableTool()", " never sent:  open62541 cancelled it first - a client's Cancel, the queue's limit or a timeout.") );
		EXPECT_TRUE( WaitLine(lines, row.Slug, "SelectPset( 7 )", ":  Good.") );
		EXPECT_EQ( Commands(station, from), vector<uint16_t>{18} );
		EXPECT_TRUE( station.ToolEnabled() );
		EXPECT_EQ( Lines(lines, row.Slug, "DisableTool()").size(), 1u );
		EXPECT_EQ( host->Calls(), 0u );
	}

	//A session's close cancels nothing:  a call whose session has gone is sent and answered, its line logged, and open62541
	//drops the result.  A stop finds two calls held, of a session gone too:  the request out keeps its answer, which the
	//controller owes before it takes MID 0003, the one behind it is Bad_Shutdown, never sent, and the server goes with no call
	//held and nothing set after.
	TEST_F( DriverCommandTests, ACallOutlivesItsSessionAndAStopLeavesNone ){
		const string slug{ "driver-command-session" };
		Controller station{ *Executor(), ControllerPort(), Station() };
		optional<DriverRow> row;
		row.emplace( slug );
		ASSERT_TRUE( WaitRevisions(*row, Subscribed) ) << OpText( Op(*row, "Revisions") );
		optional<ResultGroup> group;
		group.emplace( *row, slug, vector<std::pair<sv,jobject>>{{"Result.Torque", {}}} );
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitExecutable(*row, true) );
		let host = DriverHost::Find( slug );
		ASSERT_TRUE( host );
		Logging::Capture lines{ ELogLevel::Information };
		UA_UInt16 pset{ 7 }, job{ 12 };
		let set = Argument( pset ), selected = Argument( job );
		{
			auto client = Client( *row );
			station.Delay( 1500ms );
			Send( *client, "SelectPset", &set );
			ASSERT_TRUE( WaitUntil([&]{ UA_Client_run_iterate( *client, 10 ); return host->Calls()==1; }) ) << "held for the controller's answer";
		}
		EXPECT_EQ( host->Calls(), 1u ) << "its session closed, the call is held still";
		EXPECT_EQ( Lines(lines, slug, "SelectPset( 7 )"), vector<string>{} );
		EXPECT_TRUE( WaitLine(lines, slug, "SelectPset( 7 )", ":  Good.") ) << "answered with no session to hear it";
		EXPECT_TRUE( WaitUntil([&]{ return host->Calls()==0; }) );
		EXPECT_TRUE( WaitValue(*row, "Selected.Pset", "7") );

		let from = station.Frames().size();
		{
			auto client = Client( *row );
			station.Delay( 3s );
			Send( *client, "SelectJob", &selected );
			Send( *client, "DisableTool" );
			ASSERT_TRUE( WaitUntil([&]{ UA_Client_run_iterate( *client, 10 ); return host->Calls()==2; }) );
			ASSERT_TRUE( WaitUntil([&]{ return Commands( station, from ).size()==1; }) ) << "one request out at a time";
		}
		group.reset();//the collector's items go before the row's purge closes their client.
		row.reset();//purged:  its host stops.
		EXPECT_FALSE( DriverHost::Find(slug) );
		EXPECT_EQ( host->Calls(), 0u ) << "none held once its server has gone";
		EXPECT_EQ( Lines(lines, slug, "SelectJob( 12 )"), vector<string>{":  Good."} ) << "the request out kept its answer";
		EXPECT_EQ( Lines(lines, slug, "DisableTool()"), vector<string>{":  BadShutdown - The session is stopped."} ) << "behind it, never sent";
		EXPECT_EQ( Commands(station, from), vector<uint16_t>{38} );
		EXPECT_TRUE( station.ToolEnabled() );
		EXPECT_EQ( RevisionsOf(station, 3, from).size(), 1u ) << "MID 0003 behind the answer";
		for( let& entry : lines.Find([&]( const Logging::Entry& e ){ return e.Message().contains( slug ) && e.Message().contains( "couldn't be answered" ); }) )
			ADD_FAILURE() << entry.Message();
	}
}