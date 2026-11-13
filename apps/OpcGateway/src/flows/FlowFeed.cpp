#include "FlowFeed.h"
#include <jde/opc/uatypes/DateTime.h>
#include <jde/flows/FileTrace.h>
#include "FlowSubscriber.h"

#define let const auto

namespace Jde::Opc::Gateway{
	using Flows::TraceRecord;
	static_assert( (int)Flows::ERecordKind::Hop==FromServer::FlowRecordHop && (int)Flows::ERecordKind::Drop==FromServer::FlowRecordDrop );

	Ω stamp( TimePoint t )ι->google::protobuf::Timestamp{ return UADateTime{ t }.ToProto(); }
	Ω toProto( const Flows::NodeStatus& status )ι->FromServer::FlowStatus{
		FromServer::FlowStatus y;
		y.set_fill( status.Fill );
		y.set_shape( status.Shape );
		y.set_text( status.Text );
		return y;
	}
	Ω toProto( const Flows::NodeCounts& counts )ι->FromServer::FlowCounts{
		FromServer::FlowCounts y;
		y.set_received( counts.Received );
		for( let sent : counts.Sent )
			y.add_sent( sent );
		y.set_drops( counts.Drops );
		y.set_errors( counts.Errors );
		return y;
	}
	//A message's json as the web reads it:  each 64-bit integer a Long, then cut as the trace cuts it.
	Ω wire( jvalue&& json, uint limit, bool& cut )ι->string{
		Flows::ToWire( json );
		auto text = serialize( json );
		let kept = Flows::CutJson( text, limit ).size();
		cut = kept<text.size();
		text.resize( kept );
		return text;
	}
	//A record's, which is text:  one that does not parse goes as it is.
	Ω wire( str text, uint limit, bool& cut )ι->string{
		boost::json::parse_options options;
		options.allow_invalid_utf8 = true;//what a Function body wrote, as the trace keeps it.
		boost::system::error_code ec;
		auto json = boost::json::parse( text, ec, {}, options );
		if( !ec )
			return wire( move(json), limit, cut );
		let kept = Flows::CutJson( text, limit );
		cut = kept.size()<text.size();
		return string{ kept };
	}
	Ω ack( const FlowFeed::Run& run, const Flows::FlowState& state )ι->FromServer::FlowSubscribeAck{
		FromServer::FlowSubscribeAck y;
		y.set_version( run.Version );
		for( uint i=0; i<run.Nodes.size() && i<state.Counts.size(); ++i ){
			auto& node = *y.add_nodes();
			node.set_node( run.Nodes[i] );
			*node.mutable_status() = toProto( state.Statuses[i] );
			*node.mutable_counts() = toProto( state.Counts[i] );
		}
		return y;
	}
	//A FlowActivity's nodes and wires, serialized:  empty with neither.
	Ω body( Flows::Activity&& activity, const vector<string>& nodes, uint limit )ι->string{
		static const string none;
		let id = [&]( uint32 index )->str{ return index<nodes.size() ? nodes[index] : none; };
		FromServer::FlowActivity y;
		for( auto& node : activity.Nodes ){
			auto& to = *y.add_nodes();
			to.set_node( id(node.Node) );
			if( node.Counts.Any() )
				*to.mutable_counts() = toProto( node.Counts );
			if( node.Status )
				*to.mutable_status() = toProto( *node.Status );
			if( node.Error.size() )
				to.set_error( move(node.Error) );
		}
		for( auto& sent : activity.Wires ){
			auto& to = *y.add_wires();
			to.set_node( id(sent.Node) );
			to.set_port( (uint32_t)sent.Port );
			to.set_to( id(sent.To) );
			to.set_count( sent.Count );
			to.set_msg_id( sent.Last.Id() );
			*to.mutable_time() = stamp( sent.Time );
			bool cut{};
			to.set_json( wire(jvalue{move(sent.Last.Json)}, limit, cut) );
			to.set_cut( cut );
		}
		return y.SerializeAsString();
	}
	//A FlowActivity's hops, serialized:  appended to its nodes and wires, the two read as one message.
	Ω hops( std::deque<TraceRecord>&& records, uint dropped, uint limit )ι->string{
		FromServer::FlowActivity y;
		for( auto& r : records ){
			auto& hop = *y.add_hops();
			*hop.mutable_time() = stamp( r.Time );
			hop.set_msg_id( r.Id );
			hop.set_parent_id( r.Parent );
			hop.set_run_id( r.Run );
			hop.set_node( move(r.Node) );
			hop.set_kind( (FromServer::EFlowRecord)r.Kind );
			for( let port : r.Ports )
				hop.add_ports( (uint32_t)port );
			if( r.Json ){
				bool cut{};
				hop.set_json( wire(*r.Json, limit, cut) );
				hop.set_cut( cut );
			}
			if( r.Error.size() )
				hop.set_error( move(r.Error) );
			if( r.Status )
				*hop.mutable_status() = toProto( *r.Status );
			for( auto& block : r.Blocks )
				hop.add_blocks( move(block) );
		}
		if( dropped )
			y.set_hops_dropped( (uint32_t)dropped );
		return y.SerializeAsString();
	}

	FlowFeed::FlowFeed( FlowPK id, sp<Hist::IClock> clock, Duration live, uint valueLimit )ι:
		_id{ id }, _clock{ move(clock) }, _live{ live }, _valueLimit{ valueLimit }
	{}
	α FlowFeed::Started( Run run )ι->void{
		lg send{ _sendMutex };
		vector<sp<FlowSubscriber>> subscribers;
		{
			lg _{ _mutex };
			if( run.Serial==_run.Serial )
				return;
			_run = run;
			Disarm();
			subscribers = _subscribers;
		}
		let flow = run.Flow.lock();
		if( subscribers.empty() || !flow )
			return;
		flow->Watch( true );
		Flows::FlowState state;
		flow->TakeActivity( &state );
		let message = ack( run, state ).SerializeAsString();
		for( let& subscriber : subscribers )
			subscriber->OnAck( message );
	}
	α FlowFeed::Ended( EHttpStatus status, sv what )ι->void{
		vector<sp<FlowSubscriber>> subscribers;
		{
			lg send{ _sendMutex };
			lg _{ _mutex };
			_run = {};
			Disarm();
			_last = nullopt;
			_hops = false;
			subscribers = std::exchange( _subscribers, {} );
		}
		for( let& subscriber : subscribers )//under no lock:  the session's end comes back through Remove.
			subscriber->OnEnded( status, string{what} );
	}
	α FlowFeed::Add( sp<FlowSubscriber> subscriber, SL sl )ε->FromServer::FlowSubscribeAck{
		lg send{ _sendMutex };
		Run run;
		sp<Flows::Flow> flow;
		vector<sp<FlowSubscriber>> before;
		std::deque<TraceRecord> records;
		uint dropped{};
		{
			lg _{ _mutex };
			flow = _run.Flow.lock();
			if( !flow || !flow->Running() )
				throw Exception{ sl, ExceptionArgs{EHttpStatus::Conflict}, "Flow {} is not running.", _id };
			run = _run;
			before = _subscribers;
			records = std::exchange( _records, {} );
			dropped = std::exchange( _dropped, 0 );
			if( before.size() )
				_last = _clock->Now();
		}
		if( before.empty() )
			flow->Watch( true );
		Flows::FlowState state;
		Send( before, flow->TakeActivity(&state), run, move(records), dropped );
		lg _{ _mutex };
		if( subscriber->Hops() )
			_hops = true;
		_subscribers.push_back( move(subscriber) );
		return ack( run, state );
	}
	α FlowFeed::Remove( const FlowSubscriber* subscriber )ι->void{
		lg send{ _sendMutex };
		sp<Flows::Flow> idle;
		{
			lg _{ _mutex };
			if( !erase_if(_subscribers, [subscriber]( let& s ){ return s.get()==subscriber; }) )
				return;
			_hops = std::ranges::any_of( _subscribers, []( let& s ){ return s->Hops(); } );
			if( !_hops ){
				_records.clear();
				_dropped = 0;
			}
			if( _subscribers.empty() ){
				Disarm();
				_last = nullopt;
				idle = _run.Flow.lock();
			}
		}
		if( idle )
			idle->Watch( false );
	}
	α FlowFeed::OnActive( uint serial )ι->void{
		lg _{ _mutex };
		if( serial==_run.Serial )
			Arm();
	}
	α FlowFeed::OnRecord( uint serial, const TraceRecord& record )ι->void{
		if( !_hops.load(std::memory_order_relaxed) )
			return;
		lg _{ _mutex };
		if( serial!=_run.Serial || !_hops )
			return;
		if( _records.size()>=MaxHops ){
			_records.pop_front();
			++_dropped;
		}
		_records.push_back( record );
		Arm();
	}
	α FlowFeed::Arm()ι->void{
		if( _timer || _subscribers.empty() )
			return;
		let now = _clock->Now();
		let due = _last ? std::max( now, *_last+_live ) : now;
		_timer = _clock->Schedule( due-now, [weak=weak_from_this(), tick=++_tick]{
			if( auto self = weak.lock() )
				self->Tick( tick );
		} );
	}
	α FlowFeed::Disarm()ι->void{
		if( auto timer = std::exchange(_timer, 0); timer )
			_clock->Cancel( timer );
		++_tick;
		_records.clear();
		_dropped = 0;
	}
	α FlowFeed::Tick( uint tick )ι->void{
		lg send{ _sendMutex };
		{
			lg _{ _mutex };
			if( tick!=_tick )
				return;
			_timer = 0;
			let now = _clock->Now();
			if( _last && now<*_last+_live ){//a join pushed since the timer was set.
				Arm();
				return;
			}
			_last = now;
		}
		Push();
	}
	α FlowFeed::Push()ι->void{
		Run run;
		vector<sp<FlowSubscriber>> subscribers;
		std::deque<TraceRecord> records;
		uint dropped;
		{
			lg _{ _mutex };
			run = _run;
			subscribers = _subscribers;
			records = std::exchange( _records, {} );
			dropped = std::exchange( _dropped, 0 );
		}
		if( let flow = run.Flow.lock(); flow && subscribers.size() )
			Send( subscribers, flow->TakeActivity(), run, move(records), dropped );
	}
	α FlowFeed::Send( const vector<sp<FlowSubscriber>>& to, Flows::Activity&& activity, const Run& run, std::deque<TraceRecord>&& records, uint dropped )ι->void{
		if( to.empty() )
			return;
		let activityBody = body( move(activity), run.Nodes, _valueLimit );
		let both = records.empty() && !dropped ? string{} : activityBody+hops( move(records), dropped, _valueLimit );
		bool sent{};
		for( let& subscriber : to ){
			let& message = subscriber->Hops() && both.size() ? both : activityBody;
			if( message.empty() )
				continue;
			subscriber->OnActivity( message );
			sent = true;
		}
		if( sent )
			++_pushed;
	}
}