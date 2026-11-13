#include <jde/flows/Flow.h>
#include <deque>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <jde/fwk/process/execution.h>
#include "nodes/Nodes.h"

#define let const auto

namespace Jde::Opc::Flows{
	constexpr ELogTags _tags{ ELogTags::App };
	constexpr uint _minutesKept{ 60 };
	constexpr uint _loopGuard{ 9 };//Node-RED's:  the times one node's error with one message is caught before the message is dropped.
	constexpr uint _turn{ 1000 };//what an entry of its own takes off the queues before the strand's other work has its turn.

	//What waits at a node.  Born:  made at it, a source's, an inject's or what a catch, a status or a complete takes, and
	//sent down its output when taken;  otherwise the node receives it.
	struct Entry final{ Message Msg; bool Born{}; };
	//A message a node has received and not finished.  Watched:  as received, then as sent, for the complete nodes.
	struct Hop final{ MessageId Id{}; optional<Message> Watched; };

	//A node as its flow runs it.
	struct Slot final{
		Slot( const NodeDef& def, uint32 index, Engine& flow, Store&& context )ι:
			Def{ def }, Index{ index }, Flow{ flow }, Context{ move(context) }
		{}
		α Opened( MessageId id )ι->vector<Hop>::iterator{ return find( Open, id, &Hop::Id ); }

		const NodeDef& Def;
		const uint32 Index;
		Engine& Flow;
		up<INode> Node;//none for a type the engine runs itself.
		vector<vector<uint32>> Wires;//Def's, and a `link out`'s one to its `link in`.
		flat_set<uint32> Scope;//a catch's, a status's or a complete's.
		bool Uncaught{};//a catch for what no other takes.
		vector<uint32> Completes;//the complete nodes that take what this finishes.
		//The strand's.
		Store Context;
		vector<Hop> Open;
		flat_map<INode::TimerId,Hist::IClock::TimerId> Timers;
		uint Dropped{}, Unhandled{};
		string LastError;
		bool Dirty{};//its status is behind Dropped or Unhandled.
		bool Started{};
		//Under the queue mutex.
		std::deque<Entry> Queue;
		//Under the state mutex.
		NodeStatus Status;
		NodeCounts Total, Minute;
	};

	enum class EState : uint8{ Idle, Starting, Running, Stopped };
	struct Engine final : std::enable_shared_from_this<Engine>{
		Engine( Graph&& graph, Flow::Config&& config )ι:
			_graph{ move(graph) },
			_config{ move(config) },
			_clock{ _config.Clock ? _config.Clock : Hist::SystemClock() },
			_ids{ _config.Ids ? _config.Ids : ms<MessageIds>(_clock->Now()) },
			_global{ _config.Global ? _config.Global : ms<GlobalStore>() },
			_trace{ _config.Trace },
			_ioc{ Executor() },
			_strand{ boost::asio::make_strand(*_ioc) },
			_flowContext{ move(_config.Context), Changed({}) }{
			if( !_config.Queue )
				_config.Queue = 1;
			for( uint32 i=0; i<_graph.Nodes.size(); ++i ){
				let& def = _graph.Nodes[i];
				let context = _config.NodeContext.find( def.Id );
				auto& slot = *_slots.emplace_back( mu<Slot>(def, i, *this, Store{context==_config.NodeContext.end() ? jobject{} : move(context->second), Changed(def.Id)}) );
				slot.Wires = def.Wires;
				slot.Total.Node = slot.Minute.Node = def.Id;
			}
		}

		α Start()ι->bool{
			{
				lg _{ _queueMutex };
				if( _state!=EState::Idle )
					return false;
				_state = EState::Starting;
			}
			for( auto& slot : _slots ){
				try{
					Make( *slot );
				}
				catch( const std::exception& e ){
					Refuse( *slot, e.what() );
					lg _{ _queueMutex };
					_state = EState::Stopped;
					return false;
				}
			}
			{
				lg _{ _stateMutex };
				_minute = floor<minutes>( _clock->Now() );
			}
			{
				lg _{ _queueMutex };
				if( _state!=EState::Starting )//stopped while its nodes were made.
					return false;
				_state = EState::Running;
			}
			boost::asio::post( _strand, [self=shared_from_this()]{ self->Enter( [&]{ self->StartNodes(); } ); } );
			return true;
		}
		α Stop( absl::AnyInvocable<void()> stopped )ι->void{
			let prior = End();
			if( prior==EState::Running ){
				boost::asio::post( _strand, [self=shared_from_this(), stopped=std::move(stopped)]()mutable{
					self->Teardown();
					if( stopped )
						stopped();
				} );
			}
			else if( stopped && prior==EState::Stopped )//behind a teardown still on its way.
				boost::asio::post( _strand, [stopped=std::move(stopped)]()mutable{ stopped(); } );
			else if( stopped )
				stopped();
		}
		α Running()Ι->bool{ lg _{ _queueMutex }; return _state==EState::Running; }
		α Fault()Ι->optional<Flows::Fault>{ lg _{ _stateMutex }; return _fault; }
		α Inject( sv nodeId, const jobject* message )ε->void{
			let index = _graph.IndexOf( nodeId );
			THROW_IF( !index || _graph.Nodes[*index].Type!=ENodeType::Inject, "'{}' is not an inject node of '{}'.", nodeId, _config.Name );
			THROW_IF( !Running(), "'{}' is not running.", _config.Name );
			auto m = NewMessage( 0 );
			let own = Json::FindObject( _graph.Nodes[*index].Props, "message" );
			if( let fields = message ? message : own ){
				for( let& field : *fields ){
					if( field.key()!="_id" && field.key()!="_parent" && field.key()!="_time" )
						m.Json[field.key()] = field.value();
				}
			}
			if( !m.Json.contains("payload") ){
				jvalue time{ m.Json.at("_time") };
				m.Json["payload"] = move( time );
			}
			Enqueue( *index, move(m), true );
		}
		α Post( Slot* slot, absl::AnyInvocable<void()> f )ι->void{//`slot`:  the node whose error what escapes is;  none for the flow's own.
			boost::asio::post( _strand, [self=shared_from_this(), slot, f=std::move(f)]()mutable{ self->Enter( [&]{ self->Guard( slot, f ); } ); } );
		}
		α Status( sv nodeId )Ι->NodeStatus{
			let index = _graph.IndexOf( nodeId );
			lg _{ _stateMutex };
			return index ? _slots[*index]->Status : NodeStatus{};
		}
		α Counts()Ι->vector<NodeCounts>{
			vector<NodeCounts> y;
			lg _{ _stateMutex };
			for( let& slot : _slots )
				y.push_back( slot->Total );
			return y;
		}
		α TakeMinutes()ι->vector<MinuteCounts>{
			let now = floor<minutes>( _clock->Now() );
			lg _{ _stateMutex };
			Roll( now );
			vector<MinuteCounts> y{ std::make_move_iterator(_minutes.begin()), std::make_move_iterator(_minutes.end()) };
			_minutes.clear();
			return y;
		}

		//What a node reaches the flow through.
		α Now()Ι->TimePoint{ return _clock->Now(); }
		α Limit()Ι->uint{ return _config.Queue; }
		α Global()ι->GlobalStore&{ return *_global; }
		α FlowContext()ι->Store&{ return _flowContext; }
		α NewMessage( MessageId parent )ι->Message{
			jobject o{ {"_id", _ids->Next()} };
			if( parent )
				o["_parent"] = parent;
			o["_time"] = ToIsoString( _clock->Now() );
			return Message{ move(o) };
		}
		α CloneOf( const Message& m )ι->Message{
			Message y{ jobject{m.Json} };
			y.Json["_id"] = _ids->Next();
			y.Json["_parent"] = m.Id();
			return y;
		}
		//Any thread:  onto the node's queue, its oldest out past the bound, and a drain posted when none is on its way.
		α Enqueue( uint32 index, Message&& m, bool born )ι->void{
			auto& slot = *_slots[index];
			bool post;
			{
				lg _{ _queueMutex };
				if( _state!=EState::Running )
					return;
				slot.Queue.emplace_back( move(m), born );
				if( slot.Queue.size()>_config.Queue ){//the arrival it had is the next one's.
					_dropped.emplace_back( index, slot.Queue.front().Msg.Id() );
					slot.Queue.pop_front();
				}
				else
					_arrival.push_back( index );
				post = !_scheduled && !( _strand.running_in_this_thread() && _entries );//an entry under way drains when its work is done.
				if( post )
					_scheduled = true;
			}
			if( post )
				PostDrain();
		}
		α Send( Slot& slot, Message&& m, uint port )ι->void{
			let id = m.Id();
			if( !Entered() || !id )
				return;
			let born = slot.Opened( id )==slot.Open.end();
			if( born && _trace )
				_trace->Birth( slot.Def, m );
			Fan( slot, m, {&port, 1} );
			if( born && _trace )
				_trace->Complete( slot.Def, id );
		}
		α SendEach( Slot& slot, Message&& m, std::span<const uint> ports )ι->void{
			if( !Entered() || !m.Id() || ports.empty() )
				return;
			Fan( slot, m, ports );
		}
		α Hold( Slot& slot )ι->void{
			if( Entered() && _receiving==&slot )
				_held = true;
		}
		α Done( Slot& slot, MessageId id )ι->void{
			if( Entered() )
				Finish( slot, id );
		}
		α Drop( Slot& slot, MessageId id )ι->void{
			if( Entered() )
				Dropped( slot, id );
		}
		α Fail( Slot& slot, MessageId id, Message&& m, string&& error )ι->void{
			if( !Entered() )
				return;
			Count( slot, []( NodeCounts& c ){ ++c.Errors; } );
			if( _trace )
				_trace->Error( slot.Def, id, error );
			if( let hop = slot.Opened(id); hop!=slot.Open.end() )
				slot.Open.erase( hop );
			uint count{ 1 };
			if( let source = Json::FindObject(m.Json, "error/source"); source && Json::FindDefaultSV(*source, "id")==slot.Def.Id )
				count += Json::FindNumber<uint>( *source, "count" ).value_or( 0 );
			if( count>_loopGuard ){
				ERR( "[{}.{}]A message is dropped after {} errors in a loop:  {}", _config.Name, slot.Def.Label(), _loopGuard, error );
				Dropped( slot, id );
				return;
			}
			bool caught{};
			for( let index : _catches ){
				let& taker = *_slots[index];
				if( (!taker.Scope.empty() && !taker.Scope.contains(slot.Index)) || (taker.Uncaught && caught) )
					continue;
				auto clone = CloneOf( m );
				clone.Json["_parent"] = id;
				if( let prior = clone.Json.if_contains("error") ){
					jvalue moved{ move(*prior) };
					clone.Json["_error"] = move( moved );
				}
				clone.Json["error"] = jobject{ {"message", error}, {"source", jobject{{"id", slot.Def.Id}, {"type", ToString(slot.Def.Type)}, {"name", slot.Def.Name}, {"count", count}}} };
				Enqueue( index, move(clone), true );
				caught = true;
			}
			if( caught )
				return;
			ERR( "[{}.{}]{}", _config.Name, slot.Def.Label(), error );
			++slot.Unhandled;
			slot.LastError = move( error );
			Stale( slot );
		}
		//A message leaves a node unfinished:  off its queue, given up, or held when the flow stopped.
		α Dropped( Slot& slot, MessageId id )ι->void{
			Count( slot, []( NodeCounts& c ){ ++c.Drops; } );
			if( _trace )
				_trace->Drop( slot.Def, id );
			if( let hop = slot.Opened(id); hop!=slot.Open.end() )
				slot.Open.erase( hop );
			++slot.Dropped;
			Stale( slot );
		}
		α SetStatus( Slot& slot, NodeStatus&& status )ι->void{
			if( !Entered() )
				return;
			{
				lg _{ _stateMutex };
				if( slot.Status==status )
					return;
				slot.Status = status;
			}
			if( _trace )
				_trace->Status( slot.Def, status );
			if( slot.Def.Type==ENodeType::Status )
				return;
			for( let index : _statuses ){
				let& taker = *_slots[index];
				if( !taker.Scope.empty() && !taker.Scope.contains(slot.Index) )
					continue;
				auto m = NewMessage( 0 );
				m.Json["status"] = jobject{ {"fill", status.Fill}, {"shape", status.Shape}, {"text", status.Text},
					{"source", jobject{{"id", slot.Def.Id}, {"type", ToString(slot.Def.Type)}, {"name", slot.Def.Name}}} };
				Enqueue( index, move(m), true );
			}
		}
		α Schedule( Slot& slot, auto when, absl::AnyInvocable<void()>&& f )ι->INode::TimerId{
			if( !Entered() )
				return 0;
			let id = ++_nextTimer;
			let timer = _clock->Schedule( when, [weak=weak_from_this(), id, &slot, f=std::move(f)]()mutable{
				if( auto self = weak.lock() )
					boost::asio::dispatch( self->_strand, [self, id, &slot, f=std::move(f)]()mutable{
						self->Enter( [&]{
							if( slot.Timers.erase(id) )
								self->Guard( &slot, f );
						} );
					} );
			} );
			if( !timer )
				return 0;
			slot.Timers.emplace( id, timer );
			return id;
		}
		α Cancel( Slot& slot, INode::TimerId id )ι->bool{
			let p = Entered() ? slot.Timers.find( id ) : slot.Timers.end();
			if( p==slot.Timers.end() )
				return false;
			let timer = p->second;
			slot.Timers.erase( p );
			_clock->Cancel( timer );
			return true;
		}
	private:
		α Changed( string nodeId )ι->Store::Changed{
			if( !_config.ContextChanged )
				return {};
			return [this, nodeId=move(nodeId)]( sv key, const jvalue* value ){ _config.ContextChanged( nodeId, key, value ); };
		}
		α Make( Slot& slot )ε->void{
			let& def = slot.Def;
			if( IsHosted(def.Type) ){
				THROW_IF( !_config.Host, "Nothing here makes a '{}' node.", ToString(def.Type) );
				if( IsSource(def.Type) )
					slot.Node = _config.Host->Source( def );
				else
					slot.Node = _config.Host->Sink( def );
				THROW_IF( !slot.Node, "The host made no '{}' node.", ToString(def.Type) );
			}
			else
				slot.Node = MakeNode( def, _graph );
			if( slot.Node )
				slot.Node->_slot = &slot;
			if( def.Type==ENodeType::Catch || def.Type==ENodeType::Status || def.Type==ENodeType::Complete )
				slot.Scope = Scope( def, _graph );
			if( def.Type==ENodeType::Catch ){
				slot.Uncaught = Json::FindBool( def.Props, "uncaught" ).value_or( false );
				_catches.insert( slot.Uncaught ? _catches.end() : find_if(_catches, [&]( uint32 i ){ return _slots[i]->Uncaught; }), slot.Index );
			}
			else if( def.Type==ENodeType::Status )
				_statuses.push_back( slot.Index );
			else if( def.Type==ENodeType::Complete ){
				for( let index : slot.Scope )
					_slots[index]->Completes.push_back( slot.Index );
			}
			else if( def.Type==ENodeType::LinkOut ){
				let name = Json::FindDefaultSV( def.Props, "name" );
				let in = find_if( _graph.Nodes, [name]( const NodeDef& n ){ return n.Type==ENodeType::LinkIn && Json::FindDefaultSV(n.Props, "name")==name; } );
				THROW_IF( in==_graph.Nodes.end(), "No `link in` of the flow is named '{}'.", name );
				slot.Wires = { {(uint32)(in-_graph.Nodes.begin())} };
			}
		}
		//The flow does not start:  its fault, and the node's status, say why.
		α Refuse( Slot& slot, string message )ι->void{
			ERR( "[{}.{}]The flow is stopped:  {}", _config.Name, slot.Def.Label(), message );
			lg _{ _stateMutex };
			slot.Status = { "red", "ring", message };
			_fault = Flows::Fault{ slot.Def.Id, move(message) };
		}
		α StartNodes()ι->void{
			for( auto& slot : _slots ){
				if( !slot->Node )
					continue;
				try{
					slot->Node->Start();
					slot->Started = true;
				}
				catch( const std::exception& e ){
					Refuse( *slot, e.what() );
					End();
					Teardown();
					return;
				}
			}
		}
		α End()ι->EState{//the state it had.
			lg _{ _queueMutex };
			return std::exchange( _state, EState::Stopped );
		}
		α Teardown()ι->void{
			Check();
			_tick = _clock->Now();
			for( auto& slot : _slots ){
				for( let& [_, timer] : std::exchange(slot->Timers, {}) )
					_clock->Cancel( timer );
			}
			for( auto& slot : _slots ){
				if( std::exchange(slot->Started, false) )
					slot->Node->Stop();
			}
			vector<std::pair<uint32,MessageId>> dropped;
			{
				lg _{ _queueMutex };
				dropped = std::exchange( _dropped, {} );
				_arrival.clear();
				for( auto& slot : _slots ){
					for( let& entry : std::exchange(slot->Queue, {}) )
						dropped.emplace_back( slot->Index, entry.Msg.Id() );
				}
			}
			for( auto& slot : _slots ){
				for( let& hop : slot->Open )
					dropped.emplace_back( slot->Index, hop.Id );
			}
			for( let& [index, id] : dropped )
				Dropped( *_slots[index], id );
			_stale.clear();
		}

		α Check()Ι->void{
#ifndef NDEBUG
			ASSERT( _strand.running_in_this_thread() );
#endif
		}
		//Every entry to the engine from a node:  on the strand, and for a flow that has not stopped.
		α Entered()Ι->bool{
			Check();
			lg _{ _queueMutex };
			return _state==EState::Running;
		}
		//`f`, then the queues:  a timer's or a post's entry to the strand, and a drain's own.  One entry may come inside
		//another, a timer a clock runs on the strand:  each drains to the end, so what a timer sends is through the flow before
		//the clock moves on, but one that comes inside a drain, which goes on after it.  An entry of its own takes its turn,
		//`_turn` messages, and posts a drain for the rest, so a timer or a post from off the strand waits behind no more.
		α Enter( auto&& f )ι->void{
			Check();
			if( !Running() )
				return;
			++_entries;
			_tick = _clock->Now();
			f();
			if( !_draining )
				Drain( _entries==1 );
			--_entries;
		}
		//A timer's callback or a post's:  what escapes it is its node's error, with no message.
		α Guard( Slot* slot, absl::AnyInvocable<void()>& f )ι->void{
			try{
				f();
			}
			catch( const std::exception& e ){
				ERR( "[{}.{}]{}", _config.Name, slot ? slot->Def.Label() : string{}, e.what() );
				if( !slot )
					return;
				Count( *slot, []( NodeCounts& c ){ ++c.Errors; } );
				if( _trace )
					_trace->Error( slot->Def, 0, e.what() );
				++slot->Unhandled;
				slot->LastError = e.what();
				Stale( *slot );
			}
		}
		α Drain( bool turn )ι->void{
			_draining = true;
			uint taken{};
			for( ;; ){
				while( (!turn || taken<_turn) && Take() )
					++taken;
				if( _stale.empty() )
					break;
				for( auto slot : std::exchange(_stale, {}) ){//once a drain, however many it dropped.
					slot->Dirty = false;
					if( slot->Unhandled )
						SetStatus( *slot, {"red", "ring", Ƒ("{} uncaught, the last:  {}", slot->Unhandled, slot->LastError)} );
					else
						SetStatus( *slot, {"yellow", "ring", Ƒ("{} dropped", slot->Dropped)} );
				}
			}
			_draining = false;
			if( turn && taken==_turn ){
				{
					lg _{ _queueMutex };
					_scheduled = true;
				}
				PostDrain();
			}
		}
		α PostDrain()ι->void{ boost::asio::post( _strand, [self=shared_from_this()]{ self->Enter( []{} ); } ); }
		α Stale( Slot& slot )ι->void{
			if( !std::exchange(slot.Dirty, true) )
				_stale.push_back( &slot );
		}
		//The next arrival through its node, after what the queues dropped;  false with nothing left.
		α Take()ι->bool{
			vector<std::pair<uint32,MessageId>> dropped;
			optional<Entry> entry;
			Slot* slot{};
			{
				lg _{ _queueMutex };
				if( _state!=EState::Running )
					return false;
				dropped = std::exchange( _dropped, {} );
				if( !_arrival.empty() ){
					slot = _slots[_arrival.front()].get();
					_arrival.pop_front();
					entry = move( slot->Queue.front() );
					slot->Queue.pop_front();
				}
				else if( dropped.empty() ){
					_scheduled = false;
					return false;
				}
			}
			_tick = _clock->Now();
			for( let& [index, id] : dropped )
				Dropped( *_slots[index], id );
			if( !entry )
				return true;
			if( entry->Born )
				Send( *slot, move(entry->Msg), 0 );
			else if( slot->Node )
				Receive( *slot, entry->Msg );
			return true;
		}
		α Receive( Slot& slot, Message& m )ι->void{
			let id = m.Id();
			Count( slot, []( NodeCounts& c ){ ++c.Received; } );
			if( _trace )
				_trace->Receive( slot.Def, m );
			slot.Open.emplace_back( id, slot.Completes.empty() ? nullopt : optional{m} );
			_receiving = &slot;
			_held = false;
			try{
				slot.Node->Receive( m );
			}
			catch( const std::exception& e ){
				_receiving = nullptr;
				if( slot.Opened(id)!=slot.Open.end() )
					Fail( slot, id, move(m), e.what() );
				return;
			}
			_receiving = nullptr;
			if( !_held )
				Finish( slot, id );
		}
		α Finish( Slot& slot, MessageId id )ι->void{
			let hop = slot.Opened( id );
			if( hop==slot.Open.end() )
				return;
			let watched = move( hop->Watched );
			slot.Open.erase( hop );
			if( _trace )
				_trace->Complete( slot.Def, id );
			for( uint i=0; watched && i<slot.Completes.size(); ++i )
				Enqueue( slot.Completes[i], CloneOf(*watched), true );
		}
		//The message down the first port's first wire, and a clone down every other wire of the ports.
		α Fan( Slot& slot, Message& m, std::span<const uint> ports )ι->void{
			static const vector<uint32> none;
			optional<uint32> first;
			vector<std::pair<uint32,Message>> clones;
			for( let port : ports ){
				let& wires = port<slot.Wires.size() ? slot.Wires[port] : none;
				Count( slot, [port]( NodeCounts& c ){
					if( c.Sent.size()<=port )
						c.Sent.resize( port+1 );
					++c.Sent[port];
				} );
				let own = !first && !wires.empty();
				if( _trace )
					_trace->Send( slot.Def, port, m, own ? 1 : 0 );
				for( let wire : wires ){
					if( own && !first ){
						first = wire;
						continue;
					}
					clones.emplace_back( wire, CloneOf(m) );
					if( _trace )
						_trace->Clone( slot.Def, clones.back().second, port );
				}
			}
			if( let hop = slot.Opened(m.Id()); hop!=slot.Open.end() && hop->Watched )
				hop->Watched = m;
			if( first )
				Enqueue( *first, move(m), false );
			for( auto& [wire, clone] : clones )
				Enqueue( wire, move(clone), false );
		}
		//A figure, in the node's total and in its minute, which ends when the flow's clock passes it.
		α Count( Slot& slot, auto&& add )ι->void{
			lg _{ _stateMutex };
			Roll( floor<minutes>(_tick) );
			add( slot.Total );
			add( slot.Minute );
		}
		α Roll( TimePoint minute )ι->void{
			if( minute<=_minute )
				return;
			MinuteCounts ended{ _minute, {} };
			for( auto& slot : _slots ){
				if( slot->Minute.Any() )
					ended.Nodes.push_back( std::exchange(slot->Minute, NodeCounts{slot->Def.Id}) );
			}
			if( !ended.Nodes.empty() ){
				_minutes.push_back( move(ended) );
				if( _minutes.size()>_minutesKept )
					_minutes.pop_front();
			}
			_minute = minute;
		}

		const Graph _graph;
		Flow::Config _config;
		const sp<Hist::IClock> _clock;
		const sp<MessageIds> _ids;
		const sp<GlobalStore> _global;
		const sp<ITrace> _trace;
		const sp<boost::asio::io_context> _ioc;
		boost::asio::strand<boost::asio::io_context::executor_type> _strand;
		vector<up<Slot>> _slots;
		vector<uint32> _catches;//those for what no other takes last.
		vector<uint32> _statuses;
		//The strand's.
		Store _flowContext;
		uint _entries{};//the entries to the strand under way.
		bool _draining{};
		Slot* _receiving{};
		bool _held{};
		TimePoint _tick;//the clock, as the entry or the message in hand began.
		INode::TimerId _nextTimer{};
		vector<Slot*> _stale;
		mutable mutex _queueMutex;
		EState _state{ EState::Idle };
		std::deque<uint32> _arrival;//a node's index for each message queued, in the order they came.
		vector<std::pair<uint32,MessageId>> _dropped;//off a queue, for the strand to count.
		bool _scheduled{};//a drain is posted or running.
		mutable mutex _stateMutex;
		optional<Flows::Fault> _fault;
		TimePoint _minute;
		std::deque<MinuteCounts> _minutes;
	};

	Flow::Flow( Graph graph, Config config )ι:
		_engine{ ms<Engine>(move(graph), move(config)) }
	{}
	Flow::~Flow(){
		if( _engine )
			_engine->Stop( {} );
	}
	α Flow::operator=( Flow&& rhs )ι->Flow&{
		if( this!=&rhs ){
			if( _engine )
				_engine->Stop( {} );
			_engine = move( rhs._engine );
		}
		return *this;
	}
	α Flow::Start()ι->bool{ return _engine->Start(); }
	α Flow::Stop( absl::AnyInvocable<void()> stopped )ι->void{ _engine->Stop( std::move(stopped) ); }
	α Flow::Running()Ι->bool{ return _engine->Running(); }
	α Flow::Fault()Ι->optional<Flows::Fault>{ return _engine->Fault(); }
	α Flow::Inject( sv nodeId, const jobject* message )ε->void{ _engine->Inject( nodeId, message ); }
	α Flow::Post( absl::AnyInvocable<void()> f )ι->void{ _engine->Post( nullptr, std::move(f) ); }
	α Flow::Status( sv nodeId )Ι->NodeStatus{ return _engine->Status( nodeId ); }
	α Flow::Counts()Ι->vector<NodeCounts>{ return _engine->Counts(); }
	α Flow::TakeMinutes()ι->vector<MinuteCounts>{ return _engine->TakeMinutes(); }

	α INode::Def()Ι->const NodeDef&{ return _slot->Def; }
	α INode::Now()Ι->TimePoint{ return _slot->Flow.Now(); }
	α INode::Make( MessageId parent )ι->Message{ return _slot->Flow.NewMessage( parent ); }
	α INode::Clone( const Message& m )ι->Message{ return _slot->Flow.CloneOf( m ); }
	α INode::Send( Message&& m, uint port )ι->void{ _slot->Flow.Send( *_slot, move(m), port ); }
	α INode::SendEach( Message&& m, std::span<const uint> ports )ι->void{ _slot->Flow.SendEach( *_slot, move(m), ports ); }
	α INode::Hold()ι->void{ _slot->Flow.Hold( *_slot ); }
	α INode::Done( MessageId id )ι->void{ _slot->Flow.Done( *_slot, id ); }
	α INode::Fail( Message&& m, string error )ι->void{
		let id = m.Id();
		_slot->Flow.Fail( *_slot, id, move(m), move(error) );
	}
	α INode::Drop( MessageId id )ι->void{ _slot->Flow.Drop( *_slot, id ); }
	α INode::SetStatus( NodeStatus status )ι->void{ _slot->Flow.SetStatus( *_slot, move(status) ); }
	α INode::Schedule( Duration after, absl::AnyInvocable<void()> f )ι->TimerId{ return _slot->Flow.Schedule( *_slot, after, std::move(f) ); }
	α INode::Schedule( TimePoint due, absl::AnyInvocable<void()> f )ι->TimerId{ return _slot->Flow.Schedule( *_slot, due, std::move(f) ); }
	α INode::Cancel( TimerId id )ι->bool{ return _slot->Flow.Cancel( *_slot, id ); }
	α INode::Post( absl::AnyInvocable<void()> f )ι->void{ _slot->Flow.Post( _slot, std::move(f) ); }
	α INode::Context()ι->Store&{ return _slot->Context; }
	α INode::FlowContext()ι->Store&{ return _slot->Flow.FlowContext(); }
	α INode::Global()ι->GlobalStore&{ return _slot->Flow.Global(); }
	α INode::QueueLimit()Ι->uint{ return _slot->Flow.Limit(); }
	α ISource::Emit( Message&& m )ι->void{ _slot->Flow.Enqueue( _slot->Index, move(m), true ); }
}