#include "FlowMAwait.h"
#include <boost/uuid/random_generator.hpp>
#include <jde/fwk/exceptions/refuse.h>
#include <jde/fwk/str.h>
#include <jde/access/Authorize.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/ops/MutationAwait.h>
#include "../GatewayAppClient.h"
#include "../flows/FlowHost.h"
#include "../types/ServerCnnctn.h"
#include "HistGroupQLAwait.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };
	using QL::EMutationQL;
	using namespace FlowTable;

	α FlowMAwait::IsApplicable( const QL::MutationQL& m )ι->bool{
		using enum EMutationQL;
		if( m.CommandName==DeployCommand || m.CommandName==InjectCommand || m.CommandName==ContextCommand )
			return true;
		let table = m.TableName();
		if( table==Versions || table==Contexts || table==Runs )
			return true;
		return table==Flows && ( m.Type==Create || m.Type==Update || m.Type==Delete || m.Type==Restore || m.Type==Purge );
	}
	α FlowMAwait::Test( QL::MutationQL& m, QL::Creds executer, SL sl )->up<TAwait<jvalue>>{
		return IsApplicable( m ) ? mu<FlowMAwait>( move(m), move(executer), sl ) : nullptr;
	}

	Ω flowArgument( const QL::MutationQL& m, SL sl )ε->FlowPK{
		let id = m.TryNumber<FlowPK>( "flow" );
		REFUSE_IF( !id, "{} needs flow, a flows id.", m.CommandName );
		return *id;
	}
	//A create's, or an update's that names them:  a name that is one, and a trace level that is one.
	Ω check( const jobject& args, SL sl )ε->void{
		if( let p = args.if_contains("name"); p )
			REFUSE_IF( !p->is_string() || p->get_string().empty(), "name is required." );
		if( let p = args.if_contains("trace"); p && !p->is_null() )
			REFUSE_IF( !p->is_string() || !Flows::ToTraceLevel(p->get_string()), "trace is off, counts, errors or hops, not {}.", serialize(*p) );
	}
	//The slug is the flow's trace directory.
	Ω checkSlug( const jobject& args, SL sl )ε->string{
		let p = args.if_contains( "slug" );
		REFUSE_IF( !p || !p->is_string(), "slug is required." );
		const sv slug{ p->get_string() };
		if( auto e = FileSlugError(slug, sl); e )
			e->Throw();
		return string{ slug };
	}

	α FlowMAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			using enum EMutationQL;
			let sl = _sl;//REFUSE's.
			auto& host = FlowHost::Instance();
			let acl = AppClient()->Acl();
			if( _query.CommandName==DeployCommand ){
				_query.CheckVariables( sl );//before an argument is read:  an unbound one would read as absent.
				let flow = flowArgument( _query, sl );
				let graph = _query.FindPtr( "graph" );
				REFUSE_IF( !graph || (!graph->is_object() && !graph->is_string()), "deployFlow needs graph, the flow's graph as json or its text." );
				let commented = _query.FindPtr( "comment" );
				REFUSE_IF( commented && !commented->is_null() && !commented->is_string(), "comment is text, not {}.", serialize(*commented) );
				let comment = commented && commented->is_string() ? sv{ commented->get_string() } : sv{};
				let characters = std::ranges::count_if( comment, []( char c ){ return ((unsigned char)c & 0xC0)!=0x80; } );//UTF-8's, as the column counts them.
				REFUSE_IF( characters>2048, "comment is {} characters, over the column's 2048.", characters );
				let deployed = co_await host.Deploy( flow, jvalue{*graph}, string{comment}, QL::Creds{_creds}, sl );
				jobject y{ {"flow", flow}, {"version", deployed.Version}, {"deployed", ToIsoString(deployed.Time)}, {"userName", deployed.UserName}, {"running", deployed.Running}, {"dropped", deployed.Dropped} };
				y["fault"] = deployed.Fault ? jvalue{ jobject{{"node", deployed.Fault->NodeId.empty() ? jvalue{} : jvalue{deployed.Fault->NodeId}}, {"message", deployed.Fault->Message}} } : jvalue{};
				Resume( _query.ResultRequest ? jvalue{_query.ResultRequest->TrimColumns(y)} : jvalue{} );
				co_return;
			}
			if( _query.CommandName==InjectCommand ){
				_query.CheckVariables( sl );
				let flow = flowArgument( _query, sl );
				let node = _query.FindPtr<jstring>( "node" );
				REFUSE_IF( !node, "injectFlow needs node, an inject node's id." );
				let message = _query.FindPtr( "message" );
				REFUSE_IF( message && !message->is_null() && !message->is_object(), "message is the message to send, an object, not {}.", serialize(*message) );
				let state = host.Find( flow );
				acl->Test( "gateway", "flows", state ? state->Row.Guid : string{}, Access::ERights::Update, UserPK(), sl );//the table's for a flow that isn't there, before a caller learns of an id.
				host.Inject( flow, *node, message && message->is_object() ? &message->get_object() : nullptr, sl );
				Resume( jobject{ {"flow", flow}, {"node", *node} } );
				co_return;
			}
			if( _query.CommandName==ContextCommand ){
				_query.CheckVariables( sl );
				let scoped = _query.FindPtr( "flow" );
				let flow = _query.TryNumber<FlowPK>( "flow" );
				REFUSE_IF( scoped && !scoped->is_null() && !flow, "flow is a flows id, or null for the gateway's scope, not {}.", serialize(*scoped) );
				let named = _query.FindPtr( "node" );
				REFUSE_IF( named && !named->is_null() && !named->is_string(), "node is a node's id, or null for the flow's scope, not {}.", serialize(*named) );
				const string node{ named && named->is_string() ? sv{named->get_string()} : sv{} };
				let name = _query.FindPtr<jstring>( "name" );
				REFUSE_IF( !name, "setFlowContext needs name, the key." );
				let value = _query.FindPtr( "value" );
				REFUSE_IF( !value, "setFlowContext needs value, the json to set, or null to take the key away." );
				let state = flow ? host.Find( *flow ) : nullopt;
				acl->Test( "gateway", "flows", state ? state->Row.Guid : string{}, Access::ERights::Update, UserPK(), sl );//the table's for the gateway's scope, and for a flow that isn't there.
				co_await host.SetContext( flow, node, string{*name}, jvalue{*value}, sl );
				jobject y{ {"flow", flow ? jvalue{*flow} : jvalue{}}, {"node", node.empty() ? jvalue{} : jvalue{node}}, {"name", *name}, {"value", value->is_null() ? jvalue{} : jvalue{serialize(*value)}} };
				Resume( _query.ResultRequest ? jvalue{_query.ResultRequest->TrimColumns(y)} : jvalue{} );
				co_return;
			}
			let type = _query.Type;
			let table = _query.TableName();
			REFUSE_IF( table==Versions, "{} is written by deployFlow alone:  '{}' is refused.", table, _query.CommandName );
			REFUSE_IF( table==Contexts, "{} is the flows' own, written as they set their context, and setFlowContext's:  '{}' is refused.", table, _query.CommandName );
			REFUSE_IF( table==Runs, "{} is a replay's to write:  '{}' is refused.", table, _query.CommandName );
			_query.DBTable->Authorize( QL::StockRights(type), UserPK(), sl );//asked first:  a purge's live side runs before the statement.
			_query.CheckVariables( sl );
			let args = _query.ExtrapolateVariables();
			jobject spelled;//the columns spelled here, which the result carries over what the caller sent.
			optional<DB::Key> key;
			if( type==Create ){
				check( args, sl );
				key = DB::Key{ checkSlug(args, sl) };
				spelled["guid"] = Jde::ToString( boost::uuids::random_generator{}() );
			}
			else{
				key = type==Purge ? DB::Key{ (uint)MutationRowId(_query, sl) } : _query.GetKey( sl );
				auto rows = co_await Any( SelectFlows({.Key=*key, .IncludeDeleted=true}) );
				if( rows.empty() )
					throw Exception{ sl, ExceptionArgs{EHttpStatus::NotFound}, "Flow {} not found.", key->IsPK() ? std::to_string(key->PK()) : key->NK() };
				acl->Test( "gateway", "flows", rows.front().Guid, QL::StockRights(type), UserPK(), sl );//the flow's own row, where an admin has made one.
				if( type==Update )
					check( args, sl );
			}
			for( let& [name,value] : spelled )
				_query.Args[name] = value;

			optional<FlowRow> purged;
			if( type==Purge )
				co_await host.Purging( (FlowPK)key->PK(), purged );
			jvalue y;
			std::exception_ptr failed;//a purge's, rethrown once the host has started the flow again from its rows, which flow_purge's transaction left whole:  no co_await in a handler.
			try{
				y = co_await QL::MutationAwait{ QL::MutationQL{_query}, _creds, nullptr, sl };//the stock op:  no QL, so nothing routes it back here.
			}
			catch( const runtime_error& ){
				if( type!=Purge )
					throw;
				failed = std::current_exception();
			}
			if( failed ){
				co_await host.Changed( *key );
				std::rethrow_exception( failed );
			}
			if( type!=Purge )
				co_await host.Changed( *key );
			else if( purged )
				co_await host.Purged( move(*purged) );
			if( auto row = spelled.size() && y.is_array() && y.get_array().size() ? y.get_array()[0].if_object() : nullptr; row ){
				for( let& [name,value] : spelled )
					(*row)[name] = value;
			}
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}