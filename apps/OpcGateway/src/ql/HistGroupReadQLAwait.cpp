#include "HistGroupReadQLAwait.h"
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>
#include <jde/fwk/str.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/process/thread.h>
#include <jde/opc/UAException.h>
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
			let aggregate = Hist::FindAggregate( args.Aggregate );
			if( !aggregate )
				throw UAException{ UA_STATUSCODE_BADAGGREGATENOTSUPPORTED, Ƒ("A group's history has no aggregate '{}'.", args.Aggregate), {ELogLevel::Debug}, sl };
			//An interval ToDuration refuses, 9e12 ms or more, is one over the whole range, as 0 is, as the pass-through counts it.
			return group.ReadProcessed( {.Nodes=move(nodes), .Start=*args.Start, .End=*args.End, .Interval=Hist::ToDuration(args.Interval).value_or(Duration::zero()),
				.Aggregate=*aggregate, .Limit=args.Limit, .Continuation=args.Continuation}, sl );
		}
		return group.Read( {.Nodes=move(nodes), .Start=args.Start, .End=args.End, .Bounds=args.Bounds, .Modified=args.Modified, .Limit=args.Limit, .Continuation=args.Continuation}, sl );
	}
	//The library's reads parse the group's files synchronously, every member's records of each day they cover, each window
	//of a file read through the file awaitables and waited for.  Those waits need an executor thread free to run the read's
	//completion, so the reads run on threads of their own, not the executor's:  a pool, which ends with the process.
	struct ReadThreads final : IShutdown{
		α Post( absl::AnyInvocable<void()> f )ι->void{
			bool created{};
			{
				lg _{ _mutex };
				if( !_pool ){
					_pool = mu<boost::asio::thread_pool>( std::max(2u, std::thread::hardware_concurrency()/2) );
					created = true;
				}
				boost::asio::post( *_pool, [f=std::move(f)]() mutable { Thread::SetName( "HistRead" ); f(); } );
			}
			if( created )//outside the lock:  Shutdown takes it while the shutdown list is being walked.
				Execution::AddShutdown( this );
		}
		α Shutdown( bool /*terminate*/, SL )ι->void override{//the reads queued finish first:  each has an awaiter waiting on it.
			up<boost::asio::thread_pool> pool;
			{
				lg _{ _mutex };
				pool = move( _pool );
			}
			if( pool )
				pool->join();
		}
	private:
		up<boost::asio::thread_pool> _pool; mutex _mutex;
	};
	ReadThreads _readThreads;

	struct GroupReadAwait final : AnyAwait<Hist::ReadResult>{
		GroupReadAwait( sp<Hist::Group> group, const HistQL::GroupArgs& args, vector<Hist::NodeIndex> nodes, SRCE )ι:AnyAwait<Hist::ReadResult>{ sl }, _group{ move(group) }, _args{ args }, _nodes{ move(nodes) }{}
	private:
		α Suspend()ι->void override{
			_readThreads.Post( [this]{
				try{
					Resume( read(*_group, _args, move(_nodes), _sl) );
				}
				catch( runtime_error& e ){
					ResumeExp( move(e) );
				}
			} );
		}
		sp<Hist::Group> _group;
		const HistQL::GroupArgs& _args;//the awaiting frame's.
		vector<Hist::NodeIndex> _nodes;
	};

	α HistGroupReadQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			const HistQL::GroupArgs args{ _query, _sl };
			let host = HistHost::Instance().Gate( args.Group, Access::ERights::Read, false, UserPK(), _sl );
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
				auto page = co_await GroupReadAwait{ host->Group(), args, move(nodes), _sl };
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
	}
}