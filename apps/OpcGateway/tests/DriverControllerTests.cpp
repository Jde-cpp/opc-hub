#include <future>
#include <boost/asio/connect.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>
#include <jde/fwk/str.h>
#include <jde/fwk/process/execution.h>
#include <jde/opc/openProtocol/Session.h>
#include <jde/opc/openProtocol/tests/StandIn.h>
#include "utils/DriverCommands.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	using boost::system::error_code;
	using Tcp = boost::asio::ip::tcp;

	//A TCP relay on a loopback port of its own, between a row and its station:  a network's fault at a case's word.  Severed,
	//it closes the connection it carries and each one offered it until Mend, while the station goes on.
	struct Relay final{
		Relay( string host, uint16_t port )ε: _impl{ ms<Impl>(move(host), port) }{ boost::asio::post( _impl->Ran, [impl=_impl]{ impl->Listen(); } ); }
		Relay( const Relay& )=delete;
		~Relay(){ Run( []( Impl& impl ){ impl.Close(); } ); }
		α Url()Ι->string{ return Ƒ( "open-protocol://127.0.0.1:{}", _impl->Port ); }
		α Sever()ι->void{ Run( []( Impl& impl ){ impl.Severed = true; impl.Drop(); } ); }
		α Mend()ι->void{ Run( []( Impl& impl ){ impl.Severed = false; } ); }
	private:
		struct Impl final : std::enable_shared_from_this<Impl>{
			struct Pair{ Tcp::socket Row, Station; array<char,4096> FromRow, FromStation; bool Dropped{}; };
			Impl( string&& host, uint16_t port )ε:
				Ran{ boost::asio::make_strand(*Executor()) }, _acceptor{ Ran, Tcp::endpoint{boost::asio::ip::address_v4::loopback(), 0} }, _resolver{ Ran },
				_host{ move(host) }, _station{ std::to_string(port) }, Port{ _acceptor.local_endpoint().port() }
			{}
			α Listen()ι->void{
				_acceptor.async_accept( [self=shared_from_this()]( const error_code& ec, Tcp::socket row ){
					if( ec )
						return;//closed.
					if( !self->Severed )
						self->Dial( ms<Pair>(move(row), Tcp::socket{self->Ran}) );
					self->Listen();
				} );
			}
			α Dial( sp<Pair> pair )ι->void{
				_pairs.push_back( pair );
				_resolver.async_resolve( _host, _station, [self=shared_from_this(), pair]( const error_code& ec, const Tcp::resolver::results_type& found ){
					if( ec || pair->Dropped ){
						self->Drop( pair );
						return;
					}
					boost::asio::async_connect( pair->Station, found, [self, pair]( const error_code& ec, const Tcp::endpoint& ){
						if( ec || pair->Dropped ){
							self->Drop( pair );
							return;
						}
						error_code ignored;//as the session and the controller set theirs:  no hold of the relay's own.
						pair->Row.set_option( Tcp::no_delay{true}, ignored );
						pair->Station.set_option( Tcp::no_delay{true}, ignored );
						self->Carry( pair, pair->Row, pair->Station, pair->FromRow );
						self->Carry( pair, pair->Station, pair->Row, pair->FromStation );
					} );
				} );
			}
			α Carry( sp<Pair> pair, Tcp::socket& from, Tcp::socket& to, array<char,4096>& bytes )ι->void{
				from.async_read_some( boost::asio::buffer(bytes), [self=shared_from_this(), pair, &from, &to, &bytes]( const error_code& ec, size_t size ){
					if( ec ){
						self->Drop( pair );
						return;
					}
					boost::asio::async_write( to, boost::asio::buffer(bytes.data(), size), [self, pair, &from, &to, &bytes]( const error_code& ec, size_t ){
						if( ec )
							self->Drop( pair );
						else
							self->Carry( pair, from, to, bytes );
					} );
				} );
			}
			α Drop( const sp<Pair>& pair )ι->void{
				error_code ignored;
				pair->Dropped = true;
				pair->Row.close( ignored );
				pair->Station.close( ignored );
				std::erase( _pairs, pair );
			}
			α Drop()ι->void{
				for( let& pair : vector{_pairs} )
					Drop( pair );
			}
			α Close()ι->void{
				error_code ignored;
				_acceptor.close( ignored );
				_resolver.cancel();
				Drop();
			}
			const OpenProtocol::Strand Ran;
			bool Severed{};
		private:
			Tcp::acceptor _acceptor;
			Tcp::resolver _resolver;
			const string _host;
			const string _station;
			vector<sp<Pair>> _pairs;
		public:
			const uint16_t Port;
		};
		//On its strand, and done when this returns.
		α Run( function<void( Impl& )> f )Ι->void{
			std::promise<void> ran;
			auto done = ran.get_future();
			boost::asio::post( _impl->Ran, [impl=_impl, f=move(f), ran=std::move(ran)]()mutable{
				f( *impl );
				ran.set_value();
			} );
			done.wait_for( 5s );
		}
		const sp<Impl> _impl;
	};

	//A session of the case's own to a station, beside the row's and subscribed to its results:  what the station made,
	//whatever the row's link.
	struct Watcher final{
		Watcher( string host, uint16_t port )ι:
			_session{ boost::asio::make_strand(*Executor()), move(host), port, {}, {
				[this]( OpenProtocol::Session::Connection&& ){ _session.Request( OpenProtocol::Mid0060{}, [this]( OpenProtocol::Session::Reply&& reply ){ _subscribed = reply.has_value(); } ); },
				[this]( const OpenProtocol::Failure& ){ _subscribed = false; },
				[this]( OpenProtocol::Message&& pushed ){
					if( let result = get_if<OpenProtocol::Mid0061>(&pushed.Data) ){
						lg _{ _mutex };
						_ids.push_back( result->TighteningId.value_or(0) );
					}
				}
			} }
		{
			_session.Start();
		}
		Watcher( const Watcher& )=delete;
		~Watcher(){
			auto stopped = ms<std::promise<void>>();//the session's, which may end past this wait.
			_session.Stop( [stopped]{ stopped->set_value(); } );
			stopped->get_future().wait_for( 15s );
		}
		α WaitSubscribed()Ι->bool{ return WaitUntil( [&]{ return _subscribed.load(); }, 30s ); }
		α Ids()Ι->vector<uint32_t>{ lg _{ _mutex }; return _ids; }//the tightening ids it was pushed, as they came.
	private:
		mutable mutex _mutex;
		vector<uint32_t> _ids;
		atomic<bool> _subscribed{};
		OpenProtocol::Session _session;
	};

	//docs/open-protocol/plan.md Phase 10b (#473):  the Open Protocol driver against the bench controller, a Power Focus 6000
	//with two virtual stations on their own ports, a hand check:  each case is skipped unless /testing/openProtocolController
	//names the controller, `{ host: "192.168.0.10", stations: [4545, 4546] }`, the station that has the tool first.  What a case
	//needs of the hand at the controller, a tightening, an alarm, the tool moved, it asks for on the console and waits
	//`handTimeout` for.  With `standIn: true` the controller is the library's stand-in for one (StandIn.h), which answers as
	//the PF6000's appendix says the controller does and makes what a case asks for itself:  that is the check of these cases,
	//not of the driver.  A case expects the specification's and the appendix's account, so one the controller fails is a
	//difference to settle against them and fold into docs/open-protocol/spec.md;  what the account leaves open a case prints
	//under NOTE and expects nothing of.
	struct DriverControllerTests : DriverCommandFixture{
	protected:
		static constexpr sv Setting{ "/testing/openProtocolController" };
		//`_Op.Revisions` of a PF6000's station once the connection's seven requests are answered (appendix, Table 1):  MID 0001 at
		//6, the selected parameter set at 1 and the job info at 4, behind two refusals each, and the rest as the codec asks them.
		static constexpr sv Settled{ "0001:6 0015:1 0035:4 0052:2 0061:7 0071:3 0080:1" };
		static constexpr Duration MaxSkew{ 5min };//the driver's maxSkew, which the bench's clock is to be inside.
		Ω Find( sv name )ι->optional<sv>{ return Settings::FindSV( Ƒ("{}/{}", Setting, name) ); }
		Ω IsStandIn()ι->bool{ return Settings::FindBool( Ƒ("{}/standIn", Setting) ).value_or( false ); }
		α SetUp()->void override{
			DriverCommandFixture::SetUp();
			if( !IsSkipped() && !Find("host") && !IsStandIn() )
				GTEST_SKIP() << "No controller is named:  " << Setting << ".";
		}
		Ω Note( str text )ι->void{ std::cout << "[     NOTE ] " << text << std::endl; }
		Ω Now()ι->LocalTime{ return LocalTime{ floor<seconds>(std::chrono::current_zone()->to_local(Clock::now())) }; }

		//The controller under check and the hand at it:  the one the setting names and its operator, or the stand-in and its script.
		struct Bench final{
			Bench()ε{
				if( IsStandIn() ){
					_standIn.emplace( *Executor() );
					Ports = { _standIn->Port(0), _standIn->Port(1) };
					Wait = 15s;
					for( uint station=0; station<Ports.size(); ++station ){
						_standIn->Station( station ).Select( OpenProtocol::Mid0015{.Pset=3, .PsetChangeTime=Now()} );
						_standIn->Station( station ).Select( OpenProtocol::Mid0035{.JobId=5, .JobStatus=0, .JobBatchMode=0, .JobBatchSize=8, .JobBatchCounter=2, .Time=Now()} );
						_standIn->Station( station ).Select( OpenProtocol::Mid0052{.Vin="VIN0123456789"} );
					}
					return;
				}
				Host = string{ *Find("host") };
				if( let named = Settings::FindArray(Ƒ("{}/stations", Setting)) ){
					THROW_IF( named->size()!=Ports.size(), "{}/stations names {} ports, not a PF6000's two virtual stations'.", Setting, named->size() );
					for( uint i=0; i<Ports.size(); ++i )
						Ports[i] = named->at( i ).to_number<uint16_t>();
				}
				Wait = Settings::FindDuration( Ƒ("{}/handTimeout", Setting) ).value_or( 10min );
				if( let zone = Find("timeZone") )
					Driver = serialize( jobject{{"timeZone", *zone}} );
			}
			α Url( uint station )Ι->string{ return Ƒ( "open-protocol://{}:{}", Host, Ports[station] ); }
			//`count` rundowns at `station`, which has the tool, or has it moved to it first.
			α Tighten( uint station, uint count, sv first={} )ι->void{
				if( !_standIn ){
					Ask( Ƒ("{}make {} tightening(s) at station {}, port {}", first, count, station+1, Ports[station]) );
					return;
				}
				for( uint i=0; i<count; ++i ){
					auto result = Rundown( 0, Now() );
					result.Status = i%2 ? 0 : 1;
					_standIn->Tighten( station, move(result) );
					std::this_thread::sleep_for( 1100ms );//a controller's clock is whole seconds:  each result a time of its own.
				}
			}
			α RaiseAlarm()ι->void{
				if( _standIn )
					_standIn->Station( 0 ).Raise( {.ErrorCode="E1021", .ControllerReady=1, .ToolReady=0, .Time=Now(), .ToolHealth=2, .AlarmText="Tool over temperature"} );
				else
					Ask( Ƒ("raise an alarm at station 1, port {}, one that waits to be acknowledged", Ports[0]) );
			}
			α AcknowledgeAlarm()ι->void{
				if( _standIn )
					_standIn->Station( 0 ).AcknowledgeAlarm();
				else
					Ask( "acknowledge the alarm at the controller" );
			}
			string Host{ "127.0.0.1" };
			array<uint16_t,2> Ports{ 4545, 4546 };
			Duration Wait;//how long a case waits for the hand.
			string Driver;//the rows' `driver`:  the zone the controller's clock keeps, where the setting names one.
		private:
			α Ask( str what )Ι->void{ std::cout << "\n>>>>>>>>>> At the controller:  " << what << ".  Waiting " << duration_cast<minutes>( Wait ).count() << " min." << std::endl; }
			optional<OpenProtocol::Tests::StandIn> _standIn;
		};

		using State = flat_map<NodeId,Value>;
		static constexpr array<sv,9> StatePaths{ "Selected.Pset", "Selected.Job", "Selected.Vin", "Selected.IdentifierPart2", "Selected.IdentifierPart3", "Selected.IdentifierPart4", "Alarm.Code", "Alarm.Active", "Alarm.Time" };
		//`Selected` and `Alarm` as a web session's client reads them.
		Ω ReadState( const DriverRow& row )ε->State{
			flat_set<NodeId> nodes;
			for( let path : StatePaths )
				nodes.emplace( DriverNode(path) );
			return BlockAwait( ReadValueAwait{move(nodes), BlockAwait(UAClient::GetClient(row.Slug, SessionCredential())), UA_TIMESTAMPSTORETURN_BOTH} );
		}
		Ω Shown( const Value& v )ι->string{ return v.status ? string{ UA_StatusCode_name(v.status) } : Text( v ); }
		Ω Shown( const vector<uint32_t>& ids )ι->string{ return Str::Join( ids, " " ); }
		Ω WaitState( const DriverRow& row, function<bool( const State& )> is, Duration timeout )ι->bool{
			return WaitUntil( [&]{
				try{
					return is( ReadState(row) );
				}
				catch( const std::exception& ){
					return false;
				}
			}, timeout, 100ms );
		}
		//Until the row's alarm reads `active`, Good.
		Ω WaitAlarm( const DriverRow& row, bool active, Duration timeout )ι->bool{
			return WaitState( row, [=]( const State& read ){ let& v = read.at( DriverNode("Alarm.Active") ); return !v.status && OpFlag( v )==active; }, timeout );
		}
		Ω Results( const DriverRow& row )ι->uint{
			try{
				return OpCount( Op(row, "Results") ).value_or( 0 );
			}
			catch( const std::exception& ){
				return 0;
			}
		}
		Ω WaitAtLeast( const DriverRow& row, uint count, Duration timeout )ι->bool{ return WaitUntil( [&]{ return Results( row )>=count; }, timeout, 100ms ); }
		//A time of the controller's as the row gave it, against the gateway's clock over the case's run.
		Ω Within( optional<TimePoint> time, TimePoint from )ι->::testing::AssertionResult{
			if( !time )
				return ::testing::AssertionFailure() << "no time";
			if( *time<from-MaxSkew || *time>Clock::now()+MaxSkew )
				return ::testing::AssertionFailure() << ToIsoString( *time ) << " is over " << duration_cast<minutes>( MaxSkew ).count() << " min from the gateway's clock:  the controller's clock, or the row's zone";
			return ::testing::AssertionSuccess();
		}
	};

	//A row a station, each on its own port.  The step-down ends where the appendix's Table 1 puts a PF6000:  MID 0001 at 6,
	//so link level, the controller numbering its messages;  the selected parameter set at 1 and the job info at 4;  the VIN at
	//2, the results at 7 where the controller has 8, the alarms at 3 and the clock.  The two stations are one Power Focus
	//6000 under two station ids.  The clock is inside maxSkew of the gateway's, read in the row's zone.  Whether a
	//subscription answers what is selected at once, and the alarm's status with none active, the account leaves open.
	TEST_F( DriverControllerTests, EachStationSettlesAtTheAppendixsRevisions ){
		const Bench bench;
		array<jobject,2> identities;
		for( uint station=0; station<2; ++station ){
			SCOPED_TRACE( Ƒ("station {}", station+1) );
			const DriverRow row{ Ƒ("driver-bench-{}", station+1), bench.Driver, bench.Url(station) };
			ASSERT_TRUE( WaitConnected(row) ) << OpText( Op(row, "LastError") );
			WaitRevisions( row, Settled );
			EXPECT_EQ( OpText(Op(row, "Revisions")), Settled ) << "LastError:  " << OpText( Op(row, "LastError") );
			EXPECT_EQ( OpFlag(Op(row, "LinkLevel")), optional<bool>{true} ) << "MID 0002 at revision 6 says whether the controller numbers its messages";
			let identity = OpText( Op(row, "Identity") );
			Note( Ƒ("station {} is {}", station+1, identity) );
			identities[station] = Json::Parse( identity );
			EXPECT_EQ( Json::FindNumber<uint>(identities[station], "systemType"), optional<uint>{3} ) << "a Power Focus 6000";
			let skew = Op( row, "Skew" );
			EXPECT_EQ( skew.status, Good ) << UAException::Message( skew.status );
			EXPECT_LT( std::abs(Double(skew).value_or(0)), duration<double>{MaxSkew}.count() ) << "seconds:  the controller's clock, or the row's zone";
			Note( Ƒ("station {}'s clock is {:+.0f} s from the gateway's", station+1, Double(skew).value_or(0)) );

			WaitState( row, []( const State& read ){ return std::ranges::none_of( read, []( let& variable ){ return variable.second.status==Waiting; } ); }, 3s );
			let state = ReadState( row );
			for( let path : StatePaths ){
				let& v = state.at( DriverNode(path) );
				Note( Ƒ("station {}, nothing changed at the controller:  {} reads {}", station+1, path, Shown(v)) );
				EXPECT_TRUE( !v.status || v.status==Waiting || v.status==NotSupported ) << path << ":  " << UAException::Message( v.status );
			}
		}
		EXPECT_NE( Json::FindNumber<uint>(identities[0], "stationId"), Json::FindNumber<uint>(identities[1], "stationId") ) << "each virtual station its own id, which the walk's filter reads";
		EXPECT_TRUE( Json::FindNumber<uint>(identities[0], "stationId") && Json::FindNumber<uint>(identities[1], "stationId") );
		EXPECT_EQ( Json::FindSV(identities[0], "controllerSerialNumber"), Json::FindSV(identities[1], "controllerSerialNumber") ) << "one controller";
	}

	//A tightening at the station that has the tool lands in that station's row, by MID 0061 at revision 7, and not in the
	//other's:  every variable of `Result` its type, the result's own Good, the time the controller's clock read in the row's
	//zone, the counters adding up.  What the PF6000 sends blank, its appendix's Table 3 naming fields it lacks, reads
	//Bad_NotSupported and is printed.  A parameter set's name is the one /testing/openProtocolController/psetName gives, where
	//it gives one:  a name outside ASCII, to see the code page the controller writes, which the codec reads as Latin-1.
	TEST_F( DriverControllerTests, AStationsResultsAreItsRowsAlone ){
		Bench bench;
		const DriverRow row{ "driver-bench-1", bench.Driver, bench.Url(0) }, other{ "driver-bench-2", bench.Driver, bench.Url(1) };
		ASSERT_TRUE( WaitRevision(row, 7) && WaitRevision(other, 7) ) << OpText( Op(row, "Revisions") ) << ";  " << OpText( Op(other, "Revisions") );
		let from = Clock::now();
		bench.Tighten( 0, 2 );
		ASSERT_TRUE( WaitAtLeast(row, 2, bench.Wait) ) << Results( row ) << " result(s)";

		let read = Variables( row );
		vector<sv> blank;
		for( let& field : Fields() ){
			let& v = Of( read, Path(field.Name) );
			EXPECT_TRUE( Is(v, field.Type) ) << field.Name << " holds no " << UA_TYPES[field.Type].typeName;
			EXPECT_TRUE( !v.status || v.status==NotSupported ) << field.Name << ":  " << UAException::Message( v.status );
			if( v.status==NotSupported )
				blank.push_back( field.Name );
		}
		Note( Ƒ("sent blank at revision 7:  {}", blank.empty() ? "nothing" : Str::Join(blank, ", ")) );
		for( let own : {"Pset", "Status", "TorqueStatus", "AngleStatus", "Torque", "Angle"} )
			EXPECT_EQ( Of(read, Path(own)).status, Good ) << own << ":  " << UAException::Message( Of(read, Path(own)).status );
		EXPECT_TRUE( ResultId(read) ) << UAException::Message( Of(read, "Result.TighteningId").status );
		let time = Time( Of(read, "Result.Time") );
		EXPECT_TRUE( Within(time, from) );
		EXPECT_EQ( SourceTime(Of(read, "Result.TighteningId")), time ) << "a result's source timestamp is its own time";
		let counters = Counters( read );
		let results = Results( row );
		Note( Ƒ("{} result(s), {} of them counted as tightenings", results, counters[0].value_or(0)) );
		EXPECT_GE( counters[0].value_or(0), 1u );
		EXPECT_LE( counters[0].value_or(0), results );
		EXPECT_EQ( counters[1].value_or(0)+counters[2].value_or(0), counters[0].value_or(0) ) << "each counted OK or NOK";
		let name = Of( read, "Result.PsetName" );
		Note( Ƒ("the parameter set's name is {}", Shown(name)) );
		if( let expected = Find("psetName") )
			EXPECT_EQ( Shown(name), serialize(jvalue{*expected}) );
		EXPECT_EQ( Results(other), 0u ) << "another station's results are no result of this row's";
	}

	//An alarm raised at the controller is `Alarm` of the row, active under its code at the controller's time, and
	//acknowledged there it is active no longer, under the same code.
	TEST_F( DriverControllerTests, AnAlarmFollowsTheController ){
		Bench bench;
		const DriverRow row{ "driver-bench-1", bench.Driver, bench.Url(0) };
		ASSERT_TRUE( WaitOp(row, "Revisions", []( let& v ){ return OpText( v ).contains( "0071:" ); }) ) << "the alarms' subscription:  " << OpText( Op(row, "Revisions") ) << ";  " << OpText( Op(row, "LastError") );
		Note( Ƒ("with no alarm raised, Alarm.Active reads {}", Shown(ReadState(row).at(DriverNode("Alarm.Active")))) );
		let from = Clock::now();
		bench.RaiseAlarm();
		ASSERT_TRUE( WaitAlarm(row, true, bench.Wait) );
		let raised = ReadState( row );
		let& code = raised.at( DriverNode("Alarm.Code") );
		EXPECT_EQ( code.status, Good ) << UAException::Message( code.status );
		EXPECT_GT( Text(code).size(), 2u ) << "a code";
		Note( Ƒ("the alarm is {}", Shown(code)) );
		EXPECT_TRUE( Within(Time(raised.at(DriverNode("Alarm.Time"))), from) );

		bench.AcknowledgeAlarm();
		ASSERT_TRUE( WaitAlarm(row, false, bench.Wait) );
		EXPECT_EQ( Text(ReadState(row).at(DriverNode("Alarm.Code"))), Text(code) ) << "MID 0074 carries the code it acknowledges";
	}

	//The link to the first station drops, and both stations make results:  the first, then the second once it has the tool.
	//When the link returns the row's walk by MID 0064 goes back through both stations' results, one run of ids, and fills the
	//row with its own station's, oldest first, each once, dropping the other's by the station id MID 0065 gives at revision 7
	//(appendix §2.4).  The second row, its link standing, has its own as they were made.  A group over the first row records
	//the outage's results in the order they were made.  What each station made is what a session of the case's own there
	//was pushed, a loosening or a tightening past the ones asked for among it.  It leaves the tool at the second station.
	TEST_F( DriverControllerTests, AnOutageIsFilledFromBothStationsRun ){
		Bench bench;
		Relay relay{ bench.Host, bench.Ports[0] };
		const Watcher made{ bench.Host, bench.Ports[0] }, elsewhere{ bench.Host, bench.Ports[1] };
		const DriverRow row{ "driver-bench-1", bench.Driver, relay.Url() }, other{ "driver-bench-2", bench.Driver, bench.Url(1) };
		ASSERT_TRUE( WaitRevision(row, 7) && WaitRevision(other, 7) ) << OpText( Op(row, "Revisions") ) << ";  " << OpText( Op(other, "Revisions") );
		ASSERT_TRUE( made.WaitSubscribed() && elsewhere.WaitSubscribed() ) << "a second client at a station";
		const ResultGroup group{ row, "driver-bench", {{"Result.TighteningId", {}}} };

		bench.Tighten( 0, 1 );
		ASSERT_TRUE( WaitAtLeast(row, 1, bench.Wait) );
		relay.Sever();
		ASSERT_TRUE( WaitConnected(row, false) );
		bench.Tighten( 0, 2 );
		ASSERT_TRUE( WaitUntil([&]{ return made.Ids().size()>=3; }, bench.Wait, 100ms) ) << made.Ids().size() << " result(s) at station 1";
		bench.Tighten( 1, 2, "move the tool to station 2, then " );
		ASSERT_TRUE( WaitUntil([&]{ return elsewhere.Ids().size()>=2; }, bench.Wait, 100ms) ) << elsewhere.Ids().size() << " result(s) at station 2";
		relay.Mend();
		ASSERT_TRUE( WaitUntil([&]{ return Results( row )>=made.Ids().size(); }, 2min, 100ms) ) << Results( row ) << " of " << made.Ids().size() << " result(s);  LastError:  " << OpText( Op(row, "LastError") );
		std::this_thread::sleep_for( 1s );//what a walk wrote past the station's own is in by now.
		let own = made.Ids(), theirs = elsewhere.Ids();

		Note( Ƒ("station 1 made results {}, station 2 {}", Shown(own), Shown(theirs)) );
		auto run = own;
		run.append_range( theirs );
		std::ranges::sort( run );
		EXPECT_TRUE( std::ranges::adjacent_find(run)==run.end() ) << "one run of ids across the controller's stations";
		EXPECT_EQ( Results(row), own.size() ) << "the other station's are dropped";
		EXPECT_EQ( ResultId(Variables(row)), optional<uint>{own.back()} );
		EXPECT_EQ( OpCount(Op(row, "LostResults")), optional<uint>{0} ) << OpText( Op(row, "LastError") );
		EXPECT_EQ( OpCount(Op(row, "Reconnects")), optional<uint>{1} );
		EXPECT_TRUE( OpText(Op(row, "Revisions")).contains("0064:7") ) << OpText( Op(row, "Revisions") );
		EXPECT_TRUE( WaitUntil([&]{ return Results( other )==theirs.size(); }, 15s, 100ms) ) << Results( other ) << " of " << theirs.size() << " result(s) at station 2";
		EXPECT_EQ( ResultId(Variables(other)), optional<uint>{theirs.back()} );

		vector<uint32_t> recorded;
		let stored = [&]{
			recorded.clear();
			for( let& v : Records(group, 0, Joined) ){
				if( !v.status && Is(v, UA_TYPES_UINT32) )
					recorded.push_back( v.Get<UA_UInt32>(0) );
			}
			return recorded.size()>=own.size();
		};
		EXPECT_TRUE( WaitUntil(stored, 30s, 100ms) );
		EXPECT_EQ( recorded, own ) << "the group's records, in the order the station made them";
		if( !IsStandIn() )
			Note( "the tool is at station 2:  move it back to station 1 before another run" );
	}
}