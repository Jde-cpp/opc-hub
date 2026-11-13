#pragma once
#include <future>
#include <absl/cleanup/cleanup.h>
#include <jde/tests/Capture.h>
#include "HistGroupFixture.h"
#include "../../src/flows/FlowHost.h"
#include "../../../../libs/historian/tests/ManualClock.h"

//FlowTests' fixture, and FlowSubscribeTests':  a graph written out as a test reads it, the flows' mutations, the host's state
//of a flow, the flows resource enforced and a flow's own row scoped, and the trace's reads.
namespace Jde::Opc::Gateway::Tests{
	using Access::ERights;
	struct FlowFixture : HistGroupFixture{
	protected:
		α SetUp()->void override{
			HistGroupFixture::SetUp();
			if( !IsSkipped() && !Flows().Enabled() )
				GTEST_SKIP() << "The gateway runs no flows:  /gateway/flows.";
		}
		Ω Flows()ι->FlowHost&{ return FlowHost::Instance(); }
		//A node of a graph:  its props as json text, its wires a list of target ids per output port.
		Ω N( sv id, sv type, sv props="{}", std::initializer_list<std::initializer_list<sv>> wires={} )->jobject{
			jarray ports;
			for( let& port : wires ){
				jarray ids;
				for( let target : port )
					ids.emplace_back( target );
				ports.push_back( move(ids) );
			}
			return { {"id", id}, {"type", type}, {"props", parse(props)}, {"wires", move(ports)} };
		}
		Ω Graph( std::initializer_list<jobject> nodes )->jvalue{
			jarray y;
			for( let& node : nodes )
				y.push_back( node );
			return jobject{ {"nodes", move(y)} };
		}
		//An inject into a debug, and an inject into a hosted node of `type` on `node` of the suite's connection.
		Ω Plain()->jvalue{ return Graph( {N("in", "inject", "{}", {{"d"}}), N("d", "debug")} ); }
		Ω On( const jvalue& node )->string{ return serialize( jobject{{"connection", OpcServerSlug}, {"node", node}} ); }
		Ω Writes( const jvalue& node )->jvalue{ return Graph( {N("in", "inject", "{}", {{"w"}}), N("w", "opc out", On(node))} ); }
		Ω Reads( const jvalue& node )->jvalue{ return Graph( {N("src", "opc in", On(node), {{"d"}}), N("d", "debug")} ); }
		//A `log` node, `line`, and what it wrote since `capture` began, in order, each less its `[flow.node]` head.
		Ω Line( sv text )->jobject{ return N( "line", "log", serialize(jobject{{"template", text}}) ); }
		Ω Lines( const Logging::Capture& capture, sv flow )ι->vector<string>{
			let head = Ƒ( "[{}.line]", flow );
			vector<string> y;
			for( let& entry : capture.Find([&]( const Logging::Entry& e ){ return e.Message().starts_with( head ); }) )
				y.push_back( entry.Message().substr(head.size()) );
			return y;
		}
		Ω Errors( uint32 flow, sv node, uint count )ι->bool{ return WaitUntil( [&]{ return Counts( flow, node ).Errors==count; } ); }
		//The flows on a clock the test moves, until the cleanup puts them back on the system's, for the tests after.
		Ω OnClock( TimePoint start )ε{
			auto clock = ms<Hist::Tests::ManualClock>( start );
			Flows().Stop();
			Flows().Start( clock );
			return std::pair{ clock, absl::Cleanup{ []{ Flows().Stop(); Flows().Start(); } } };
		}
		//The hosts an `http request` may reach, until the cleanup, which leaves none.
		Ω Allow( jarray hosts )ε->void{ Settings::Set( "/gateway/flows/http/allow", move(hosts) ); }

		Ω CreateFlow( sv slug, bool enabled=true )ε->uint32{
			return Id( Mutate("createFlow( name:$name, slug:$slug, enabled:$enabled ){ id }", {{"name", Ƒ("Flow {}", slug)}, {"slug", slug}, {"enabled", enabled}}) );
		}
		Ω PurgeFlow( uint32 flow )ι->void{
			try{
				Mutate( "purgeFlow( id:$id )", {{"id", flow}} );
			}
			catch( const std::exception& e ){
				WARNT( ELogTags::Test, "purgeFlow({}):  {}", flow, e.what() );
			}
		}
		static constexpr sv DeployQL{ "deployFlow( flow:$flow, graph:$graph, comment:$comment ){ flow version userName running dropped fault{ node message } }" };
		Ω Deploy( uint32 flow, const jvalue& graph, sv comment={} )ε->jobject{
			return Mutate( DeployQL, {{"flow", flow}, {"graph", graph}, {"comment", comment.empty() ? jvalue{} : jvalue{comment}}} ).as_object();
		}
		//Through the gateway's socket, so under the suite's own web session, which an ungrouped `opc in` is read with.
		Ω DeployBySession( uint32 flow, const jvalue& graph )ε->jobject{
			return Socket().QuerySync( string{DeployQL}, {{"flow", flow}, {"graph", graph}, {"comment", nullptr}} ).as_object();
		}
		Ω Inject( uint32 flow, sv node="in", jvalue message={} )ε->void{
			Mutate( "injectFlow( flow:$flow, node:$node, message:$message ){ node }", {{"flow", flow}, {"node", node}, {"message", move(message)}} );
		}
		//What f's refusal said and its status;  none when it didn't throw.
		struct Refusal final{ EHttpStatus Status{ EHttpStatus::None }; string What; };
		Ω Refused( function<void()> f )->Refusal{
			try{
				f();
			}
			catch( const Exception& e ){
				return { e.HttpStatus(), e.what() };
			}
			return {};
		}
		Ω Has( str text, sv part )ι->::testing::AssertionResult{
			return text.find( part )!=string::npos ? ::testing::AssertionSuccess() : ::testing::AssertionFailure() << "'" << text << "' lacks '" << part << "'";
		}
		Ω Running( uint32 flow )ι->bool{ let state = Flows().Find( flow ); return state && state->Running; }
		Ω Fault( uint32 flow )ι->Flows::Fault{ let state = Flows().Find( flow ); return state && state->Fault ? *state->Fault : Flows::Fault{}; }
		Ω Counts( uint32 flow, sv node )ι->Flows::NodeCounts{
			for( auto& counts : Flows().Counts(flow) ){
				if( counts.Node==node )
					return counts;
			}
			return {};
		}
		Ω Received( uint32 flow, sv node, uint count )ι->bool{ return WaitUntil( [&]{ return Counts( flow, node ).Received==count; } ); }
		//What the node has sent from each port, once it is `sent`:  a message is counted in before the node sends it on.
		Ω Sent( uint32 flow, sv node, const vector<uint>& sent )ι->vector<uint>{
			WaitUntil( [&]{ return Counts( flow, node ).Sent==sent; } );
			return Counts( flow, node ).Sent;
		}
		Ω Versions( uint32 flow )ε->jarray{ return QL().QuerySync<jarray>( "flowVersions( flowId:$flow ){ version graph identityId userName comment }", {{"flow", flow}}, User() ); }
		Ω Contexts( uint32 flow )ε->jarray{ return QL().QuerySync<jarray>( "flowContexts( flowId:$flow ){ nodeId name value }", {{"flow", flow}}, User() ); }
		//setFlowContext:  a null flow is the gateway's scope, a null node the flow's.
		Ω SetContext( const jvalue& flow, const jvalue& node, sv name, const jvalue& value )ε->jobject{
			return Mutate( "setFlowContext( flow:$flow, node:$node, name:$name, value:$value ){ flow node name value }", {{"flow", flow}, {"node", node}, {"name", name}, {"value", value}} ).as_object();
		}
		//The row's value, compared as C++ compares:  none with no row.
		Ω Held( uint32 flow, sv node, sv name )ε->string{
			for( let& row : Contexts(flow) ){
				if( let& o = row.as_object(); Json::AsSV(o, "name")==name && Json::FindDefaultSV(o, "nodeId")==node )
					return Json::AsString( o, "value" );
			}
			return {};
		}
		Ω Execute( string sql )ε->void{ DS()->ExecuteSync( {move(sql), {}} ); }//mysql's text protocol, with no params:  a trigger can't be prepared.
		//The table under another name until the cleanup, so each statement that names it fails.
		Ω Renamed( sv table )ε{
			let name = GetViewPtr( table )->SqlName();
			Execute( Ƒ("alter table {} rename to {}_away", name, name) );
			return absl::Cleanup{ [name]{ try{ Execute( Ƒ("alter table {}_away rename to {}", name, name) ); }catch( const std::exception& e ){ ADD_FAILURE() << name << " wasn't named again:  " << e.what(); } } };
		}
		//A trigger that fails each insert of the context key `name` until the cleanup:  sqlite's or mysql's, the dialects the suite runs on.
		Ω FailingInserts( sv name )ε{
			let table = GetViewPtr( FlowTable::Contexts )->SqlName();
			let& syntax = DS()->Syntax();
			THROW_IF( syntax.HasProcs() && !syntax.HasUnsigned(), "No trigger is written here for SQL Server." );
			Execute( syntax.HasProcs()
				? Ƒ( "create trigger flow_tests_fail before insert on {} for each row begin if new.name='{}' then signal sqlstate '45000' set message_text='refused by the test'; end if; end", table, name )
				: Ƒ( "create trigger flow_tests_fail before insert on {} when new.name='{}' begin select raise(abort, 'refused by the test'); end", table, name ) );
			return absl::Cleanup{ []{ try{ Execute( "drop trigger flow_tests_fail" ); }catch( const std::exception& e ){ ADD_FAILURE() << "the trigger wasn't dropped:  " << e.what(); } } };
		}
		Ω Row( uint32 flow )ε->jobject{
			let rows = QL().QuerySync<jarray>( "flows( id:$id ){ id guid version enabled deleted }", {{"id", flow}}, User() );
			THROW_IF( rows.size()!=1, "Flow {} has {} rows.", flow, rows.size() );
			return rows[0].as_object();
		}

		//The flows resource enforced, with the suite's user granted everything on it through a role of its own, or soft-deleted
		//again, as it ships;  and a flow's own row, its guid the criteria, enforced with `allowed`, which stands in for the
		//root for that flow alone - as HistGroupFixture's two for a group.
		Ω EnforceFlows( bool on )ε->void{
			EnforceResource( "gateway", "flows", false );
			if( !on )
				return;
			auto app = AppClient();
			GrantRole( *app, "GatewayFlowEditor", "gateway", "flows", {}, ERights::All, app->UserPK() );
			EnforceResource( "gateway", "flows", true );
		}
		Ω EnforcedFlows()ε{
			absl::Cleanup open = []{ try{ EnforceFlows( false ); }catch( const std::exception& e ){ ADD_FAILURE() << "flows wasn't opened again:  " << e.what(); } };
			EnforceFlows( true );
			return open;
		}
		Ω ScopeFlow( uint32 flow, ERights allowed )ε->void{
			let app = AppClient();
			let criteria = Flows().Find( flow )->Row.Guid;
			app->QuerySync<jvalue>( "createResource( schemaName:\"gateway\", name:$name, slug:\"flows\", criteria:$criteria, allowed:255 )", {{"name", Ƒ("flows[{}]", flow)}, {"criteria", criteria}} );
			GrantRole( *app, Ƒ("GatewayFlow{}", flow), "gateway", "flows", criteria, allowed | ERights::Administer, app->UserPK() );
			EnforceResource( "gateway", "flows", true, criteria );//a row is created unenforced.
		}
		Ω UnscopeFlow( uint32 flow )ε->void{ EnforceResource( "gateway", "flows", false, Flows().Find(flow)->Row.Guid ); }

		//The trace (#451):  the flow's buffer written now, and the reads.
		Ω FlushTrace( sv slug )->void{
			std::promise<void> done;
			auto wait = done.get_future();
			Flows().Store()->Flush( slug, [&]{ done.set_value(); } );
			ASSERT_EQ( wait.wait_for(10s), std::future_status::ready ) << "the trace's flush did not finish";
		}
		static constexpr sv RecordColumns{ "time msgId parentId runId node kind ports json cut error status{ fill shape text } blocks" };
		Ω Trace( jobject args )ε->jobject{
			string arguments;
			for( let& [name, _] : args )
				arguments += Ƒ( "{}{}:${}", arguments.empty() ? "" : ", ", name, name );
			return QL().QuerySync<jobject>( Ƒ("flowTrace( {} ){{ continuation records{{ {} }} }}", arguments, RecordColumns), move(args), User() );
		}
		//Each record as `kind node`, in order.
		Ω Lines( const jobject& result )ε->vector<string>{
			vector<string> y;
			for( let& record : Json::AsArray(result, "records") )
				y.push_back( Ƒ("{} {}", Json::AsSV(record.as_object(), "kind"), Json::AsSV(record.as_object(), "node")) );
			return y;
		}
		//The flow's records since an hour ago, flushed first:  until there are `count` of them.
		Ω Traced( uint32 flow, sv slug, uint count )->jobject{
			jobject y;
			WaitUntil( [&]{ FlushTrace( slug ); y = Trace( {{"flow", flow}, {"start", ToIsoString(Clock::now()-1h)}} ); return Json::AsArray(y, "records").size()>=count; } );
			return y;
		}
	};
}