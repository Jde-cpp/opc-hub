#include <jde/web/client/http/ClientHttpAwait.h>
#include <jde/web/client/http/ClientHttpResException.h>
#include "utils/DriverCommands.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	//docs/open-protocol/plan.md Phase 10a (#472):  the Open Protocol driver against Jarrekstar's open-protocol-device-simulator
	//at the commit the plan pins, a hand check on a developer's box:  each case is skipped unless /testing/openProtocolSimulator
	//names the simulator's ports.  A case makes its own rows, drives the simulator through its HTTP API, and leaves the
	//simulator's revision profile, mode, parameter set, tool and jobs as it found them;  its VIN stays the last one downloaded,
	//which nothing of the simulator's clears.  Where the simulator's frames are not the specification's tables' is
	//libs/opc/openProtocol/tests/SimulatorTests.cpp's.
	struct DriverSimulatorTests : DriverCommandFixture{
	protected:
		using verb = boost::beast::http::verb;
		Ω Port( sv name )ι->optional<PortType>{ return Settings::FindNumber<PortType>( Ƒ("/testing/openProtocolSimulator/{}", name) ); }
		Ω Host()ι->string{ return string{ Settings::FindSV("/testing/openProtocolSimulator/host").value_or("127.0.0.1") }; }
		α SetUp()->void override{
			DriverCommandFixture::SetUp();
			if( !IsSkipped() && !(Port("port") && Port("httpPort")) )
				GTEST_SKIP() << "No simulator is named:  /testing/openProtocolSimulator.";
		}
		//The simulator's HTTP API:  its answer, an error status thrown with its body.
		Ω Api( verb method, string target, const jvalue& body={} )ε->jvalue{
			using namespace Web::Client;
			let res = BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{Host(), move(target), body.is_null() ? string{} : serialize(body), *Port("httpPort"), {.ContentType="application/json", .Verb=method, .IsSsl=false}} );
			return res.Body().empty() ? jvalue{} : Json::ParseValue( res.Body() );
		}
		Ω State()ε->jobject{ return Json::AsObject( Api(verb::get, "/state") ); }
		//What its profile gives the fields no tightening sets, each as the wire's integer.
		Ω Samples()ε->jobject{ return Json::AsObject( Api(verb::get, "/protocol/profile") ).at( "samples" ).as_object(); }
		Ω Sample( const jobject& samples, sv name, double scale=1 )ε->double{ return samples.at( name ).to_number<double>()/scale; }
		//One tightening at a torque and an angle of the case's, which the simulator makes its limits and its targets.
		Ω Tighten( bool ok )ε->void{ Api( verb::post, "/simulate/tightening", jobject{{"torque", Target}, {"angle", TargetAngle}, {"ok", ok}} ); }
		static constexpr double Target{ 12.5 }, TargetAngle{ 40 };
		//Its revision profile with every family at any revision it implements, or at 1 alone, its default:  the families as they
		//were when this goes.  It takes the whole profile back under the version it gave it at.
		struct Profile final{
			Profile( bool lifted )ε:_families{ Put([=]( jobject& families ){
				for( auto& family : families ){
					family.value().as_object()["policy"] = lifted ? "any_implemented" : "exact";
					family.value().as_object()["revision"] = 1;
				}
			}) }
			{}
			~Profile(){
				try{
					Put( [this]( jobject& families ){ families = _families; } );
				}
				catch( const std::exception& e ){
					ADD_FAILURE() << "the simulator's profile is not as it was:  " << e.what();
				}
			}
			Profile( const Profile& )=delete;
		private:
			Ω Put( const function<void( jobject& )>& edit )ε->jobject{//the families before the edit.
				auto profile = Json::AsObject( Api(verb::get, "/protocol/profile") );
				auto& families = profile.at( "families" ).as_object();
				auto was = families;
				edit( families );
				Api( verb::put, "/protocol/profile", profile );
				return was;
			}
			const jobject _families;
		};
		//A row for the simulator under the ceilings given, `"0002":3,"0061":7`.  It names no time zone:  the simulator's clock
		//is this box's.
		Ω Driver( sv ceilings )ι->string{ return Ƒ( "{{\"revisions\":{{{}}}}}", ceilings ); }
		Ω Simulator()ι->string{ return Ƒ( "open-protocol://{}:{}", Host(), *Port("port") ); }
		//Once the connection's requests are answered:  MID 0070 is the last asked, and the simulator has none.
		static constexpr sv NoAlarms{ "MID 0004 to MID 0070:  code 99, Unknown MID." };
		Ω WaitSettled( const DriverRow& row )ι->bool{ return WaitOp( row, "LastError", []( let& v ){ return OpText( v )==NoAlarms; } ); }
		Ω Number( const Value& v )ι->optional<double>{
			let json = v.hasValue && !v.IsEmpty() ? v.ValueJson( nullptr ) : jvalue{};
			return json.is_number() ? optional<double>{ json.to_number<double>() } : nullopt;
		}
		Ω String( const Value& v )ι->optional<string>{ return Is( v, UA_TYPES_STRING ) ? optional<string>{ ToString(v.Get<UA_String>(0)) } : nullopt; }
	};

	//A row with no ceiling against the simulator's default profile, each family at revision 1 alone, is the step-down's own
	//check:  MID 0001 from 6, MID 0060 from 7 and the three selections' subscriptions from their own, each refused with MID
	//0004 code 97 until 1 is asked.  MID 0002 at 1 is the controller's name alone.  The simulator has no MID 0070 and no MID
	//0080, so `Alarm` and `Skew` read Bad_NotSupported and the link stands.  With the profile lifted and MID 0002 held at 3,
	//each settles at the highest revision the simulator has, and the identity has the versions revision 3 adds.
	TEST_F( DriverSimulatorTests, ARowStepsDownToTheRevisionsTheSimulatorHas ){
		let state = State();
		let identity = []( const DriverRow& row ){ return Json::Parse( OpText(Op(row, "Identity")) ); };
		{
			const Profile profile{ false };
			const DriverRow row{ "driver-simulator", {}, Simulator() };
			ASSERT_TRUE( WaitSettled(row) ) << OpText( Op(row, "LastError") );
			EXPECT_EQ( OpText(Op(row, "Revisions")), "0001:1 0015:1 0035:1 0052:1 0061:1" );
			EXPECT_EQ( OpFlag(Op(row, "Connected")), optional<bool>{true} );
			EXPECT_EQ( OpFlag(Op(row, "LinkLevel")), optional<bool>{false} );
			EXPECT_EQ( identity(row), (jobject{{"controllerName", state.at("controller_name")}}) );
			EXPECT_EQ( Op(row, "Skew").status, NotSupported );
			let read = Variables( row );
			for( let& field : Fields() ){
				let& v = Of( read, Path(field.Name) );
				EXPECT_EQ( v.status, field.Since>1 ? NotSupported : Waiting ) << field.Name << ": " << UAException::Message( v.status );
			}
			EXPECT_TRUE( WaitValue(row, "Selected.Pset", serialize(state.at("current_pset_id"))) ) << "MID 0015 at 1, which the simulator sends as the subscription is accepted";
			EXPECT_EQ( ReadValue(BlockAwait(UAClient::GetClient(row.Slug, SessionCredential())), DriverNode("Alarm.Code")).status, NotSupported );
		}
		const Profile profile{ true };
		let samples = Samples();
		const DriverRow row{ "driver-simulator", Driver("\"0002\":3"), Simulator() };
		ASSERT_TRUE( WaitSettled(row) ) << OpText( Op(row, "LastError") );
		EXPECT_EQ( OpText(Op(row, "Revisions")), "0001:3 0015:2 0035:5 0052:2 0061:7" );
		EXPECT_EQ( identity(row), (jobject{{"controllerName", state.at("controller_name")}, {"supplierCode", state.at("supplier_code")}, {"openProtocolVersion", samples.at("open_protocol_version")},
			{"controllerSoftwareVersion", samples.at("controller_software_version")}, {"toolSoftwareVersion", samples.at("tool_software_version")}}) );
		EXPECT_TRUE( WaitValue(row, "Selected.IdentifierPart2", serialize(samples.at("identifier_part_2"))) ) << "MID 0052 at 2";
	}

	//MID 0061 at each revision the simulator has, 1 to 7, chosen by the row's ceiling with the tightening family at any revision,
	//lands in `Result`:  every field the revision carries Good and of its type, the rest Bad_NotSupported.  The torque and the
	//angle posted are the limits and the targets, the status is the one asked for, the time is the simulator's clock read in
	//this box's zone, and what no tightening sets is the profile's samples, the angle among it at revision 7, whose decimal
	//the driver takes.
	TEST_F( DriverSimulatorTests, AResultOfEachRevisionLandsInResult ){
		const Profile profile{ true };
		let samples = Samples();
		optional<uint> last;
		for( uint16_t revision=1; revision<=7; ++revision ){
			SCOPED_TRACE( Ƒ("revision {}", revision) );
			const DriverRow row{ Ƒ("driver-simulator-{}", revision), Driver(Ƒ("\"0002\":3,\"0061\":{}", revision)), Simulator() };
			ASSERT_TRUE( WaitSettled(row) ) << OpText( Op(row, "LastError") );
			EXPECT_EQ( OpText(Op(row, "Revisions")), Ƒ("0001:3 0015:2 0035:5 0052:2 0061:{}", revision) );
			let ok = revision%2==1;
			let state = State();
			let posted = Clock::now();
			Tighten( ok );
			ASSERT_TRUE( WaitResults(row, 1) ) << OpCount( Op(row, "Results") ).value_or( 0 );
			let read = Variables( row );
			let number = [&]( sv name ){ return Number( Of(read, Path(name)) ); };
			let text = [&]( sv name ){ return String( Of(read, Path(name)) ); };
			for( let& field : Fields() ){
				let& v = Of( read, Path(field.Name) );
				EXPECT_TRUE( Is(v, field.Type) ) << field.Name << " holds no " << UA_TYPES[field.Type].typeName;
				EXPECT_EQ( v.status, field.Since>revision ? NotSupported : Good ) << field.Name << ": " << UAException::Message( v.status );
			}
			for( let limit : {"TorqueMin", "TorqueMax", "TorqueFinalTarget"} )
				EXPECT_EQ( number(limit), optional<double>{Target} ) << limit;
			for( let limit : {"AngleMin", "AngleMax", "AngleFinalTarget"} )
				EXPECT_EQ( number(limit), optional<double>{TargetAngle} ) << limit;
			EXPECT_NEAR( number("Torque").value_or(0), Target, Target/2 );
			EXPECT_EQ( Of(read, "Result.Status").Get<UA_Boolean>(0), ok );
			EXPECT_EQ( number("Pset"), optional<double>{state.at("current_pset_id").to_number<double>()} );
			EXPECT_EQ( text("Vin"), optional<string>{state.at("vehicle_id").is_string() ? string{state.at("vehicle_id").get_string()} : string{}} );
			let id = ResultId( read );
			ASSERT_TRUE( id );
			if( last )
				EXPECT_EQ( *id, *last+1 ) << "the simulator's ids run up";
			last = id;
			let time = Time( Of(read, "Result.Time") );
			ASSERT_TRUE( time );
			EXPECT_LT( abs(*time-posted), 5s ) << ToIsoString( *time ) << ", the simulator's clock read in this box's zone";
			EXPECT_EQ( SourceTime(Of(read, "Result.TighteningId")), time );
			EXPECT_EQ( Of(read, "Result.PsetChangeTime").status, NotSupported ) << "sent blank";
			EXPECT_EQ( Counters(read), (array<optional<uint>,3>{1, ok ? 1 : 0, ok ? 0 : 1}) );
			if( revision>=2 ){
				EXPECT_EQ( number("Strategy"), optional<double>{Sample(samples, "tightening_strategy")} );
				EXPECT_EQ( number("StrategyOptions"), optional<double>{Sample(samples, "tightening_strategy_options")} );
				EXPECT_EQ( number("RundownAngle"), optional<double>{Sample(samples, "rundown_angle")} );
				EXPECT_EQ( number("CurrentMonitoringValue"), optional<double>{Sample(samples, "current_monitoring_value")} );
				EXPECT_EQ( number("SelfTapTorque"), optional<double>{Sample(samples, "self_tap_torque", 100)} );
				EXPECT_EQ( number("PrevailTorque"), optional<double>{Sample(samples, "prevail_torque", 100)} );
				EXPECT_EQ( number("JobSequenceNumber"), optional<double>{Sample(samples, "job_sequence_number")} );
				EXPECT_EQ( text("ToolSerialNumber"), optional<string>{Json::AsString(samples, "tool_serial_number")} );
			}
			if( revision>=3 ){
				EXPECT_EQ( text("PsetName"), optional<string>{Ƒ("PSET {}", serialize(state.at("current_pset_id")))} ) << "the simulator's name for a result's set, not the set's own";
				EXPECT_EQ( number("TorqueUnit"), optional<double>{Sample(samples, "torque_unit")} );
				EXPECT_EQ( number("ResultType"), optional<double>{Sample(samples, "tightening_result_type")} );
			}
			if( revision>=4 ){
				EXPECT_EQ( text("IdentifierPart2"), optional<string>{Json::AsString(samples, "identifier_part_2")} );
				EXPECT_EQ( text("IdentifierPart3"), optional<string>{Json::AsString(samples, "identifier_part_3")} );
				EXPECT_EQ( text("IdentifierPart4"), optional<string>{Json::AsString(samples, "identifier_part_4")} );
			}
			if( revision>=5 )
				EXPECT_EQ( text("CustomerErrorCode"), optional<string>{Json::AsString(samples, "customer_tightening_error_code")} );
			if( revision>=6 ){
				EXPECT_EQ( number("PrevailTorqueCompensateValue"), optional<double>{Sample(samples, "prevail_torque_compensate", 100)} );
				EXPECT_EQ( number("TighteningErrorStatus2"), optional<double>{Sample(samples, "tightening_error_status_2")} );
			}
			if( revision==7 ){
				EXPECT_EQ( number("CompensatedAngle"), optional<double>{Sample(samples, "compensated_angle", 100)} );
				EXPECT_EQ( number("Angle"), optional<double>{Sample(samples, "final_angle_decimal", 100)} ) << "revision 7's final angle decimal";
			}
			else
				EXPECT_NEAR( number("Angle").value_or(0), TargetAngle, TargetAngle/2 );
		}
	}

	//The five commands through the QL's `call`, each answered Good on the simulator's MID 0005, and the simulator's own
	//account of itself after each:  a parameter set, which `Selected` follows by MID 0015 and a result then carries;  one the
	//simulator lacks, refused with MID 0004 code 2, Bad_InvalidState;  the tool locked, so the simulator makes no tightening,
	//and freed;  a VIN, which `Selected` follows by MID 0052 and a result carries;  and a job of four digits by MID 0038 at 2,
	//refused with code 20 until the simulator is in its Job mode, its MID 0004 under the request's revision, and then taken,
	//`Selected` following by MID 0035.
	TEST_F( DriverSimulatorTests, TheCommandsReachTheSimulator ){
		constexpr uint job{ 4720 };
		let was = State();
		let mode = [&]( sv to ){ Api( verb::post, "/config/operation-mode", jobject{{"mode", to}} ); };
		let forget = [&]{ try{ Api( verb::delete_, Ƒ("/jobs/{}", job) ); }catch( const Web::Client::ClientHttpResException& ){} };
		absl::Cleanup restore = [&]{
			try{
				mode( Json::AsSV(was, "operation_mode") );
				forget();
				Api( verb::post, Ƒ("/psets/{}/select", serialize(was.at("current_pset_id"))) );
			}
			catch( const std::exception& e ){
				ADD_FAILURE() << "the simulator is not as it was:  " << e.what();
			}
		};
		const Profile profile{ true };
		mode( "pset" );
		const DriverRow row{ "driver-simulator-commands", Driver("\"0002\":3"), Simulator() };
		ASSERT_TRUE( WaitSettled(row) ) << OpText( Op(row, "LastError") );
		const ResultGroup group{ row, "driver-simulator-commands", {{"Result.Torque", {}}} };
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitExecutable(row, true) ) << "the grant reaches the gateway's authorizer";

		uint pset{};
		for( let& known : Api(verb::get, "/psets").as_array() ){
			if( let id = Json::AsNumber<uint>(known.as_object(), "id"); id!=was.at("current_pset_id").to_number<uint>() )
				pset = id;
		}
		ASSERT_TRUE( pset ) << "the simulator has one parameter set alone";
		EXPECT_EQ( Call(row, "SelectPset", {pset}), Good );
		EXPECT_EQ( State().at("current_pset_id").to_number<uint>(), pset );
		EXPECT_TRUE( WaitValue(row, "Selected.Pset", std::to_string(pset)) ) << "MID 0015, as the simulator reports the set it took";
		EXPECT_EQ( Call(row, "SelectPset", {999}), UA_STATUSCODE_BADINVALIDSTATE );
		EXPECT_EQ( OpText(Op(row, "LastError")), "MID 0004 to MID 0018:  code 2, Parameter set ID not present." );
		EXPECT_EQ( State().at("current_pset_id").to_number<uint>(), pset );

		EXPECT_EQ( Call(row, "DisableTool"), Good );
		EXPECT_EQ( State().at("tool_enabled"), jvalue{false} );
		EXPECT_THROW( Tighten(true), Web::Client::ClientHttpResException ) << "a locked tool makes no tightening";
		EXPECT_EQ( Call(row, "EnableTool"), Good );
		EXPECT_EQ( State().at("tool_enabled"), jvalue{true} );

		let vin = Ƒ( "VIN{}", duration_cast<seconds>(Clock::now().time_since_epoch()).count()%1'000'000 );//new at each run, so the simulator has a change to report.
		EXPECT_EQ( Call(row, "DownloadVin", {jvalue{vin}}), Good );
		EXPECT_EQ( State().at("vehicle_id"), jvalue{vin} );
		EXPECT_TRUE( WaitValue(row, "Selected.Vin", serialize(jvalue{vin})) ) << "MID 0052";

		Tighten( true );
		ASSERT_TRUE( WaitResults(row, 1) );
		let read = Variables( row );
		EXPECT_EQ( String(Of(read, "Result.Vin")), optional<string>{vin} );
		EXPECT_EQ( Number(Of(read, "Result.Pset")), optional<double>{pset} );

		forget();
		Api( verb::post, "/jobs", jobject{{"id", job}, {"name", "Simulator check"}, {"forced_order", 1}, {"first_tightening_timeout", 0}, {"job_timeout", 0}, {"batch_count_mode", 0},
			{"lock_at_job_done", false}, {"use_line_control", false}, {"repeat_job", false}, {"loosening_mode", 0}, {"repair_mode", 0},
			{"steps", jarray{jobject{{"channel_id", was.at("channel_id")}, {"pset_id", pset}, {"auto_value", false}, {"batch_size", 2}}}}} );
		EXPECT_EQ( Call(row, "SelectJob", {job}), UA_STATUSCODE_BADINVALIDSTATE ) << "outside its Job mode";
		EXPECT_EQ( OpText(Op(row, "LastError")), "MID 0004 to MID 0038:  code 20, Job can not be set." );
		EXPECT_EQ( OpFlag(Op(row, "Connected")), optional<bool>{true} ) << "a refusal under the request's revision is read, and is no fault of the link's";
		mode( "job" );
		EXPECT_EQ( Call(row, "SelectJob", {job}), Good );
		EXPECT_EQ( State().at("current_job_id"), jvalue{job} );
		EXPECT_TRUE( WaitValue(row, "Selected.Job", std::to_string(job)) ) << "MID 0035";
		EXPECT_TRUE( WaitRevisions(row, "0001:3 0015:2 0018:1 0035:5 0038:2 0042:1 0043:1 0050:1 0052:2 0061:7") ) << OpText( Op(row, "Revisions") );
	}

	//A group over the row records a run of the simulator's, its automatic tightenings a few a second, some of them NOK:
	//every result's id and count, in the order made, each timed apart from the last though the simulator's clock is whole
	//seconds, and the counters add up.
	TEST_F( DriverSimulatorTests, AGroupRecordsARun ){
		const Profile profile{ true };
		const DriverRow row{ "driver-simulator-run", Driver("\"0002\":3"), Simulator() };
		ASSERT_TRUE( WaitSettled(row) ) << OpText( Op(row, "LastError") );
		const ResultGroup group{ row, "driver-simulator-run", {{"Result.TighteningId", {}}, {"Counters.Total", {}}} };
		enum : uint{ TighteningId, Total };
		std::this_thread::sleep_for( 1100ms );//the simulator's clock is whole seconds:  the run's first result is timed past each member's first value.
		const TimePoint t0{ floor<seconds>(Clock::now()) };

		constexpr uint run{ 12 };
		let results = []( const DriverRow& row ){ return OpCount( Op(row, "Results") ).value_or( 0 ); };
		Api( verb::post, "/auto-tightening/start", jobject{{"interval_ms", 150}, {"duration_ms", 50}, {"failure_rate", 0.3}} );
		absl::Cleanup stop = [&]{ try{ Api( verb::post, "/auto-tightening/stop" ); }catch( const std::exception& e ){ ADD_FAILURE() << "the simulator's run goes on:  " << e.what(); } };
		let reached = WaitUntil( [&]{ return results( row )>=run; }, 60s );
		std::move( stop ).Invoke();
		ASSERT_TRUE( reached ) << results( row ) << " results";
		uint made{};
		ASSERT_TRUE( WaitUntil([&]{ let before = std::exchange( made, results(row) ); std::this_thread::sleep_for( 500ms ); return before==made && results( row )==made; }, 30s) ) << "the run never stopped";

		ASSERT_NO_THROW( WaitStored(group.Host, group.Members[TighteningId], t0, made) );
		ASSERT_NO_THROW( WaitStored(group.Host, group.Members[Total], t0, made) );
		let ids = Records( group, TighteningId, t0 );
		let totals = Records( group, Total, t0 );
		ASSERT_EQ( ids.size(), made );
		ASSERT_EQ( totals.size(), made );
		for( uint i=0; i<made; ++i ){
			SCOPED_TRACE( i );
			ASSERT_TRUE( Is(ids[i], UA_TYPES_UINT32) && Is(totals[i], UA_TYPES_UINT64) );
			EXPECT_EQ( totals[i].Get<UA_UInt64>(0), i+1 );
			EXPECT_EQ( SourceTime(totals[i]), SourceTime(ids[i]) ) << "one result, one time";
			if( i ){
				EXPECT_EQ( ids[i].Get<UA_UInt32>(0), ids[i-1].Get<UA_UInt32>(0)+1 ) << "none lost";
				EXPECT_GT( SourceTime(ids[i]), SourceTime(ids[i-1]) ) << "distinct and increasing";
			}
		}
		let counters = Counters( Variables(row) );
		EXPECT_EQ( counters[0], optional<uint>{made} );
		EXPECT_EQ( counters[1].value_or(0)+counters[2].value_or(0), made ) << "each result OK or NOK";
		EXPECT_GT( counters[1].value_or(0), 0u );
	}
}