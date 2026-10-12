#include "HistGroupReadQLAwait.h"
#include <jde/fwk/str.h>
#include <jde/access/Authorize.h>
#include <jde/opc/UAException.h>
#include "../GatewayAppClient.h"
#include "../hist/HistHost.h"
#include "HistQL.h"

#define let const auto
namespace Jde::Opc::Gateway{
	α HistGroupReadQLAwait::IsApplicable( const QL::TableQL& q )ι->bool{
		return q.JsonName=="history" && HistQL::Target(q)!=HistQL::ETarget::Opc;
	}

	Ω read( Hist::Group& group, const HistQL::GroupArgs& args, vector<Hist::NodeIndex>&& nodes, SL sl )ε->Hist::ReadResult{
		using enum HistQL::EMode;
		if( args.Mode==AtTime )
			return group.ReadAtTime( {.Nodes=move(nodes), .Times=args.Times, .SimpleBounds=true, .Limit=args.Limit, .Continuation=args.Continuation}, sl );
		if( args.Mode==Processed ){
			let aggregate = HistQL::FindAggregate( args.Aggregate );
			if( !aggregate )
				throw UAException{ UA_STATUSCODE_BADAGGREGATENOTSUPPORTED, Ƒ("A group's history has no aggregate '{}'.", args.Aggregate), {ELogLevel::Debug}, sl };
			//An interval ToDuration refuses, 9e12 ms or more, is one over the whole range, as 0 is, as the pass-through counts it.
			return group.ReadProcessed( {.Nodes=move(nodes), .Start=*args.Start, .End=*args.End, .Interval=Hist::ToDuration(args.Interval).value_or(Duration::zero()),
				.Aggregate=*aggregate, .Limit=args.Limit, .Continuation=args.Continuation}, sl );
		}
		return group.Read( {.Nodes=move(nodes), .Start=args.Start, .End=args.End, .Bounds=args.Bounds, .Modified=args.Modified, .Limit=args.Limit, .Continuation=args.Continuation}, sl );
	}

	α HistGroupReadQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			const HistQL::GroupArgs args{ _query, _sl };
			AppClient()->Acl()->Test( "gateway", "histGroups", std::to_string(args.Group), Access::ERights::Read, UserPK(), _sl );
			auto& hist = HistHost::Instance();
			hist.TestEnabled( _sl );
			let host = hist.Find( args.Group );
			if( !host )
				throw Exception{ _sl, ExceptionArgs{EHttpStatus::NotFound}, "Group {} isn't one this gateway historizes.", args.Group };
			let indexes = host->Indexes( args.Nodes, _sl );
			vector<StatusCode> statuses( indexes.size(), UA_STATUSCODE_GOOD );
			vector<Hist::NodeIndex> nodes;
			flat_map<Hist::NodeIndex,uint> slots;
			for( uint i=0; i<indexes.size(); ++i ){
				if( !indexes[i] ){
					statuses[i] = UA_STATUSCODE_BADNODEIDUNKNOWN;
					continue;
				}
				slots.emplace( *indexes[i], i );
				nodes.push_back( *indexes[i] );
			}
			vector<HistQL::ReadValue> values;
			string continuation;
			if( nodes.size() ){
				auto page = read( *host->Group(), args, move(nodes), _sl );
				values.reserve( page.Values.size() );
				for( auto& v : page.Values ){
					HistQL::ReadValue value{ slots.at(v.Value.node_index()), Hist::ToUA(v.Value), v.Bound, v.Value.has_heartbeat() };
					if( v.Modification )
						value.Modified = HistQL::Modification{ v.Modification->Time, (UA_HistoryUpdateType)v.Modification->Type, move(v.Modification->UserName) };
					values.push_back( move(value) );
				}
				if( page.NoData ){
					for( uint i=0; i<statuses.size(); ++i ){
						if( indexes[i] )
							statuses[i] = UA_STATUSCODE_GOODNODATA;
					}
				}
				if( page.Continuation.size() )
					continuation = Str::Encode64( page.Continuation, true );
			}
			Resume( HistQL::ToJson(_query, HistQL::NodesJson(args.Nodes), move(values), continuation, statuses) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
		co_return;
	}
}