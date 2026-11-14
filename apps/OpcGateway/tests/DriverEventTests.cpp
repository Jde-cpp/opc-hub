#include <jde/fwk/process/execution.h>
#include <jde/opc/proto/opc.Common.h>
#include "utils/DriverResults.h"
#include "utils/GatewayClientSocket.h"
#include "../src/types/proto/opc.FromServer.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	using OpenProtocol::Controller;
	using Jde::Web::Client::ClientSocketAwait;

	//docs/open-protocol/plan.md Phase 9 (#471):  each result the Open Protocol driver writes as an event
	//(drivers/openProtocol/OpenProtocolDriver.h) - a TighteningResultEventType from `Result`, heard through the socket's
	//SubscribeEvents on a web session's client (clients spec *Events*, #424), the way the Events tab hears one.
	struct DriverEventTests : DriverResultFixture{
	protected:
		using Event = FromServer::EventValues;
		enum : int{ EventId, EventType, SourceNode, SourceName, Time, ReceiveTime, Message, Severity, Base };//the base fields, as EventFilter::Base orders them.
		static constexpr sv TypeName{ "TighteningResultEventType" };
		struct Listener final : IListener{
			α OnData( string, NodeId, const Proto::Value& )ι->void override{}
			α OnEvent( const Event& event )ι->void override{ lg _{ Mutex }; Events.push_back( event ); }
			mutex Mutex; vector<Event> Events;
		};
		//An event item on `node` of the row's server:  the base fields, then every variable of `Result` in the order the driver
		//writes them, each spelled by `clause`.  Ended as it goes, and its item gone before the row's purge closes its client.
		struct Subscription final{
			Subscription( const DriverRow& row, NodeId node, function<string(sv)> clause )ε:Slug{ row.Slug }, Node{ move(node) }{
				vector<string> select;
				for( let& variable : TighteningResult::Variables() )
					select.push_back( clause(variable.Name) );
				let ack = BlockAwait<ClientSocketAwait<FromServer::SubscriptionAck>,FromServer::SubscriptionAck>( Socket().SubscribeEvents(Slug, Node, select, Heard) );
				THROW_IF( ack.results_size()!=1 || ack.results(0).status_code(), "the event item on {} was refused:  {}", Node.ToString(), ack.results_size() ? UAException::Message(ack.results(0).status_code()) : "no result" );
			}
			Subscription( const Subscription& )=delete;
			~Subscription(){
				try{
					let end = BlockAwait<ClientSocketAwait<FromServer::UnsubscribeAck>,FromServer::UnsubscribeAck>( Socket().UnsubscribeEvents(Slug, Node) );
					EXPECT_EQ( end.successes_size(), 1 ) << Node.ToString();
					let gone = WaitUntil( [&]{
						return std::ranges::none_of( UAClient::LiveClients(), [&]( let& client ){ return client->Slug()==Slug && client->TryMonitoredNodes() && client->TryMonitoredNodes()->Handles( {Node} ).size(); } );
					} );
					EXPECT_TRUE( gone ) << "the event item on " << Node.ToString() << " outlived its subscription";
				}
				catch( const std::exception& e ){
					ADD_FAILURE() << "UnsubscribeEvents( " << Node.ToString() << " ): " << e.what();
				}
			}
			α Count()Ι->uint{ lg _{ Heard->Mutex }; return Heard->Events.size(); }
			α Wait( uint count )Ι->bool{ return WaitUntil( [&]{ return Count()>=count; } ); }
			α At( uint i )Ι->Event{ lg _{ Heard->Mutex }; return Heard->Events.at( i ); }
			const ServerCnnctnNK Slug;
			const NodeId Node;
			const sp<Listener> Heard{ ms<Listener>() };
		};
		//A variable of `Result` selected by its name, on BaseEventType, as the Events tab's list spells one, and on the event's own type.
		Ω ByName( sv name )ι->string{ return Ƒ( "{}:{}", ServerCnnctn::DriverBrowseNs, name ); }
		Ω OnType( sv name )ι->string{ return Ƒ( "ns={0};s={1}/{0}:{2}", ServerCnnctn::DriverBrowseNs, TypeName, name ); }
		Ω Field( const Event& event, sv name )ε->const Proto::Value&{
			let& variables = TighteningResult::Variables();
			let p = std::ranges::find( variables, name, &TighteningResult::Variable::Name );
			THROW_IF( p==variables.end(), "`Result` has no {}.", name );
			return event.fields( Base+(int)(p-variables.begin()) );
		}
		Ω Has( const Proto::Value& field )ι->bool{ return field.of_case()!=Proto::Value::OF_NOT_SET; }
		Ω Text( const Proto::Value& field )ι->string{//as a read's value compares, DriverResultTests':  nothing for an empty field.
			try{
				return Has( field ) ? serialize( ProtoUtils::ToVariant(field).ToJson() ) : string{};
			}
			catch( const std::exception& e ){
				return e.what();
			}
		}
		Ω When( const Proto::Value& field )ι->optional<TimePoint>{ return field.has_date() ? optional<TimePoint>{ Protobuf::ToTimePoint(field.date()) } : nullopt; }
		Ω Id( const Event& event )ε->optional<uint>{ let& id = Field( event, "TighteningId" ); return Has( id ) ? optional<uint>{ id.uint32() } : nullopt; }
		//The fields the driver gives every event:  the type, `Result` as the source by its name, the result's time, the message and the severity.
		Ω ExpectBase( const Event& event, const DriverRow& row, const NodeId& node, TimePoint time, sv message, uint severity )ι->void{
			EXPECT_EQ( event.opc_id(), row.Slug );
			EXPECT_EQ( ProtoUtils::ToNodeId(event.node()), node );
			ASSERT_EQ( event.fields_size(), Base+(int)TighteningResult::Variables().size() ) << "the base fields and every variable of `Result`";
			EXPECT_EQ( event.fields(EventId).byte_string().size(), 16u );
			EXPECT_EQ( ProtoUtils::ToNodeId(event.fields(EventType).node()), DriverNode(TypeName) );
			EXPECT_EQ( ProtoUtils::ToNodeId(event.fields(SourceNode).node()), DriverNode("Result") );
			EXPECT_EQ( event.fields(SourceName).string_value(), "Result" );
			EXPECT_EQ( When(event.fields(Time)), optional<TimePoint>{time} ) << "the result's source timestamp";
			EXPECT_EQ( event.fields(Message).localized_text().text(), message );
			EXPECT_EQ( event.fields(Severity).uint16(), severity );
		}
	};

	//A result is an event from `Result`, heard there and at the Server object, which emits every event:  of the type the driver
	//declares, BaseEventType's subtype with an optional property per variable of `Result`;  its Time the result's source
	//timestamp;  OK at severity 100 and NOK at 500, each with the parameter set;  and each variable of `Result` a field with the
	//result's value, selected by its name alone at the one item and on the event's type at the other.  Two results in a second
	//are events a millisecond apart, the controller's own second in the field.  A report to `Selected` or `Alarm` is no event.
	TEST_F( DriverEventTests, AResultIsAnEventWithItsFields ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-events", NewYork };
		ASSERT_TRUE( WaitRevision(row, 7) );

		let ask = [&]( sv path, sv fields ){ return Socket().QuerySync( Ƒ("node( opc: $opc, id: $id ){{ {} }}", fields), {{"opc", row.Slug}, {"id", DriverNode(path).ToJson()}} ).as_object(); };
		let references = [&]( sv path ){ return ask( path, "references{ direction refType target }" ).at( "references" ).as_array(); };
		let refers = [&]( const jarray& rows, sv direction, UA_UInt32 refType, UA_UInt32 target ){
			return std::ranges::any_of( rows, [&]( let& r ){ let& o = r.as_object(); return Json::AsSV( o, "direction" )==direction && NodeId{ o.at("refType") }==NodeId{ refType } && NodeId{ o.at("target") }==NodeId{ target }; } );
		};
		EXPECT_EQ( Json::FindNumber<uint>(ask("Result", "eventNotifier"), "eventNotifier"), optional<uint>{UA_EVENTNOTIFIER_SUBSCRIBE_TO_EVENT} );
		EXPECT_EQ( Json::FindNumber<uint>(ask("Counters", "eventNotifier"), "eventNotifier"), optional<uint>{0} ) << "`Result` alone";
		EXPECT_EQ( Json::FindNumber<uint>(ask(TypeName, "nodeClass"), "nodeClass"), optional<uint>{UA_NODECLASS_OBJECTTYPE} );
		EXPECT_TRUE( refers(references(TypeName), "Inverse", UA_NS0ID_HASSUBTYPE, UA_NS0ID_BASEEVENTTYPE) ) << serialize( references(TypeName) );
		vector<string> properties, variables;
		for( let& property : Json::AsArray(ask(TypeName, "properties{ name nodeClass }"), "properties") ){
			properties.push_back( Json::AsString(property.as_object(), "name") );
			EXPECT_EQ( Json::FindNumber<uint>(property.as_object(), "nodeClass"), UA_NODECLASS_VARIABLE ) << properties.back();
		}
		for( let& variable : TighteningResult::Variables() )
			variables.emplace_back( variable.Name );
		std::ranges::sort( properties );
		std::ranges::sort( variables );
		EXPECT_EQ( properties, variables ) << "a property per variable of `Result`";
		let torque = Ƒ( "{}.Torque", TypeName );
		EXPECT_EQ( NodeId{ask(torque, "dataType").at("dataType")}, NodeId{UA_NS0ID_DOUBLE} ) << "of its variable's type";
		EXPECT_TRUE( refers(references(torque), "Forward", UA_NS0ID_HASMODELLINGRULE, UA_NS0ID_MODELLINGRULE_OPTIONAL) ) << serialize( references(torque) );

		const Subscription atResult{ row, DriverNode("Result"), &ByName };
		const Subscription atServer{ row, NodeId{UA_NS0ID_SERVER}, &OnType };
		let at = LocalTime{ local_days{year{2026}/7/4}+10h+15min+30s };//on the controller's clock:  14:15:30 UTC in New York's summer.
		const TimePoint source{ sys_days{year{2026}/7/4}+14h+15min+30s };
		//Event `i` at both items, one event:  its base fields, and every variable's value as Rundown gives it, `own` the controller's time.
		let expect = [&]( uint i, uint32_t id, TimePoint time, TimePoint own, bool ok, TimePoint before ){
			ASSERT_TRUE( atResult.Wait(i+1) && atServer.Wait(i+1) ) << atResult.Count() << " at `Result`, " << atServer.Count() << " at the Server object";
			for( let item : {&atResult, &atServer} ){
				SCOPED_TRACE( item->Node.ToString() );
				let event = item->At( i );
				ASSERT_NO_FATAL_FAILURE( ExpectBase(event, row, item->Node, time, ok ? "OK, parameter set 3" : "NOK, parameter set 3", ok ? 100 : 500) );
				let received = When( event.fields(ReceiveTime) );
				ASSERT_TRUE( received );
				EXPECT_GE( *received, before-1ms );
				EXPECT_LE( *received, Clock::now() ) << "the gateway's receipt";
				for( let& field : Fields() ){
					let value = field.Name=="Angle" ? jvalue{ 355.25 } : field.Name=="Status" ? jvalue{ ok } : field.Value;//revision 7's final angle decimal.
					EXPECT_EQ( Text(Field(event, field.Name)), serialize(value) ) << field.Name;
				}
				EXPECT_EQ( Id(event), optional<uint>{id} );
				EXPECT_EQ( When(Field(event, "Time")), optional<TimePoint>{own} ) << "the controller's own";
				EXPECT_EQ( When(Field(event, "PsetChangeTime")), optional<TimePoint>{PsetChanged} );
			}
			EXPECT_EQ( atResult.At(i).fields(EventId).byte_string(), atServer.At(i).fields(EventId).byte_string() ) << "one event, heard twice";
		};
		auto before = Clock::now();
		station.Push( Rundown(100, at) );
		ASSERT_NO_FATAL_FAILURE( expect(0, 100, source, source, true, before) );

		before = Clock::now();
		auto nok = Rundown( 101, at+1min );
		nok.Status = 0;
		station.Push( move(nok) );
		ASSERT_NO_FATAL_FAILURE( expect(1, 101, source+1min, source+1min, false, before) );

		before = Clock::now();
		station.Push( Rundown(102, at+1min) );
		ASSERT_NO_FATAL_FAILURE( expect(2, 102, source+1min+1ms, source+1min, true, before) ) << "the same second";
		EXPECT_NE( atResult.At(1).fields(EventId).byte_string(), atResult.At(2).fields(EventId).byte_string() );

		station.Select( OpenProtocol::Mid0015{.Pset=4} );
		station.Raise( OpenProtocol::Mid0071{.ErrorCode="E404", .Time=at+2min} );
		ASSERT_TRUE( WaitUntil([&]{ return !ReadValue( BlockAwait(UAClient::GetClient(row.Slug, SessionCredential())), DriverNode("Alarm.Active") ).status; }) ) << "the alarm never landed";
		station.Push( Rundown(103, at+3min) );
		ASSERT_TRUE( atResult.Wait(4) );
		EXPECT_EQ( Id(atResult.At(3)), optional<uint>{103} ) << "nothing between the results:  a report is no event";
		EXPECT_EQ( atResult.Count(), 4u );
	}

	//An event carries what its result does.  At revision 1 it has the fields that revision carries, and each of the rest is
	//empty.  A field the result sent blank is empty too;  a result with no time is timed at its receipt;  and one with no
	//tightening status is no OK:  severity 500, and a message that claims neither, with no parameter set where it sent none.
	TEST_F( DriverEventTests, AnEventLeavesOutWhatItsResultLacks ){
		auto oldest = Station();
		oldest.Revisions[60] = 1;
		Controller station{ *Executor(), ControllerPort(), move(oldest) };
		const DriverRow row{ "driver-event-lacks", NewYork };
		ASSERT_TRUE( WaitRevision(row, 1) );
		const Subscription heard{ row, DriverNode("Result"), &ByName };
		let at = LocalTime{ local_days{year{2026}/7/4}+10h+15min+30s };
		const TimePoint source{ sys_days{year{2026}/7/4}+14h+15min+30s };

		station.Push( Rundown(200, at) );
		ASSERT_TRUE( heard.Wait(1) );
		{
			let event = heard.At( 0 );
			ASSERT_NO_FATAL_FAILURE( ExpectBase(event, row, heard.Node, source, "OK, parameter set 3", 100) );
			uint carried{};
			for( let& variable : TighteningResult::Variables() ){
				EXPECT_EQ( Has(Field(event, variable.Name)), variable.Since==1 ) << variable.Name;
				carried += variable.Since==1;
			}
			EXPECT_EQ( carried, 20u ) << "Table 108's twenty-three, but the cell id, the channel id and the controller name";
			EXPECT_EQ( Text(Field(event, "Torque")), serialize(jvalue{12.23}) );
			EXPECT_EQ( Text(Field(event, "Angle")), serialize(jvalue{355.0}) ) << "whole degrees below revision 7";
			EXPECT_EQ( Id(event), optional<uint>{200} );
		}

		auto blank = Rundown( 201, nullopt );
		blank.Torque.reset();
		blank.Status.reset();
		blank.Pset.reset();
		let before = Clock::now();
		station.Push( move(blank) );
		ASSERT_TRUE( heard.Wait(2) );
		let event = heard.At( 1 );
		ASSERT_EQ( event.fields_size(), Base+(int)TighteningResult::Variables().size() );
		let time = When( event.fields(Time) );
		ASSERT_TRUE( time );
		EXPECT_GE( *time, Ms(before) ) << "no time:  its receipt";
		EXPECT_LE( *time, Clock::now() );
		EXPECT_EQ( event.fields(Message).localized_text().text(), "No status" );
		EXPECT_EQ( event.fields(Severity).uint16(), 500u );
		for( let name : {"Torque", "Status", "Pset", "Time"} )
			EXPECT_FALSE( Has(Field(event, name)) ) << name << ":  sent blank";
		EXPECT_EQ( Text(Field(event, "TorqueMax")), serialize(jvalue{14.0}) );
		EXPECT_EQ( Id(event), optional<uint>{201} );
	}

	//A fill raises an event per result written, in the order they are written, each timed as its result is:  the results the
	//walk found, then the one held, and none for the controller's other station's.  A found result is a MID 0065, with no
	//limits, so its event has none, and the held one's, a MID 0061's, has.
	TEST_F( DriverEventTests, AFillRaisesAnEventPerWrittenResult ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-event-fill", NewYork };
		ASSERT_TRUE( WaitRevision(row, 7) );
		const Subscription heard{ row, DriverNode("Result"), &ByName };
		const TimePoint t0{ ceil<seconds>(Clock::now()) };
		let rundown = [&]( uint32_t id, seconds after, double torque, uint8_t status=1 ){
			auto y = Rundown( id, LocalTime{floor<seconds>(std::chrono::locate_zone("America/New_York")->to_local(t0+after))} );
			y.Torque = torque;
			y.Status = status;
			return y;
		};
		station.Push( rundown(100, 0s, 10) );
		ASSERT_TRUE( WaitResults(row, 1) );

		ASSERT_TRUE( Sever(station, row) );
		station.Push( rundown(102, 10s, 10.5) );
		station.Keep( rundown(103, 20s, 99), 8, "Station 8" );
		station.Push( rundown(105, 30s, 11, 0) );
		station.Keep( rundown(106, 40s, 99), 8, "Station 8" );
		//Back, the clock's reply held, so a result is pushed after the subscription and before the first MID 0064.
		let before = station.Frames().size();
		Mend( station );
		station.Delay( 300ms, 3 );//MID 0002, MID 0060's MID 0005 and MID 0081.
		ASSERT_TRUE( WaitUntil([&]{ return RevisionsOf( station, 80, before ).size()>0; }) ) << "the link never came back";
		station.Push( rundown(108, 50s, 11.5) );
		ASSERT_TRUE( WaitResults(row, 4) ) << OpCount( Op(row, "Results") ).value_or( 0 ) << " result(s)";

		ASSERT_TRUE( heard.Wait(4) ) << heard.Count() << " event(s)";
		struct Written final{ uint32_t Id; double Torque; seconds After; bool Ok; bool Found; };
		const array<Written,4> written{{ {100, 10, 0s, true, false}, {102, 10.5, 10s, true, true}, {105, 11, 30s, false, true}, {108, 11.5, 50s, true, false} }};
		for( uint i=0; i<written.size(); ++i ){
			SCOPED_TRACE( i );
			let& result = written[i];
			let event = heard.At( i );
			ASSERT_NO_FATAL_FAILURE( ExpectBase(event, row, heard.Node, t0+result.After, result.Ok ? "OK, parameter set 3" : "NOK, parameter set 3", result.Ok ? 100 : 500) );
			EXPECT_EQ( Id(event), optional<uint>{result.Id} ) << "oldest first, the held one last";
			EXPECT_EQ( Text(Field(event, "Torque")), serialize(jvalue{result.Torque}) );
			EXPECT_EQ( When(Field(event, "Time")), optional<TimePoint>{t0+result.After} );
			EXPECT_EQ( Has(Field(event, "TorqueMax")), !result.Found ) << "a found result carries no limits";
			EXPECT_EQ( Has(Field(event, "PsetName")), !result.Found );
		}
		EXPECT_EQ( heard.Count(), 4u ) << "none for the other station's";
	}
}