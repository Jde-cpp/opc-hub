#include <deque>
#include "Nodes.h"
#include "Props.h"

#define let const auto

namespace Jde::Opc::Flows{
	namespace{
		constexpr std::array<sv,2> _modes{ "delay", "rate" };

		//Node-RED's two modes, on the flow's clock.  "delay":  each message held for `duration`, and sent on in the order they
		//came:  each has a timer, and a timer sends the oldest, since two that fall due together may come back in either order.
		//"rate":  `count` messages per `per`, evenly spaced;  one that comes too soon waits its turn, or with `drop` ends here.
		//What waits, in either mode, is bounded as a queue is, its oldest dropped.  Every wait is an interval's timer, never a
		//time compared, so a step of the wall clock neither stalls nor bursts it.
		struct Delay final : INode{
			Delay( const jobject& props )ε:
				_rate{ Prop::Choice(props, "mode", _modes, 0)==1 },
				_drop{ Prop::Flag(props, "drop") }{
				if( !_rate ){
					_interval = Prop::Wait( props, "duration" );
					return;
				}
				_interval = Prop::Wait( props, "per" )/Prop::FindCount( props, "count" ).value_or( 1 );
				THROW_IF( _interval==Duration::zero(), "`count` per `per` is more than the clock counts." );
			}
			α Receive( Message& m )ε->void override{
				if( !_rate ){
					Hold();
					Wait( move(m) );
					auto id = ms<TimerId>();
					*id = Schedule( _interval, [this, id]{
						if( let p = find(_timers, *id); p!=_timers.end() )
							_timers.erase( p );
						Release();
					} );
					_timers.push_back( *id );
					return;
				}
				if( _open ){
					_open = false;
					Send( move(m) );
					Schedule( _interval, [this]{ Turn(); } );
					return;
				}
				if( _drop )
					return;
				Hold();
				Wait( move(m) );
			}
			α Stop()ι->void override{ _waiting.clear(); }
		private:
			α Wait( Message&& m )ι->void{
				_waiting.push_back( move(m) );
				if( _waiting.size()<=QueueLimit() )
					return;
				Drop( _waiting.front().Id() );
				_waiting.pop_front();
				if( !_rate ){//its timer goes with it, the oldest.
					Cancel( _timers.front() );
					_timers.pop_front();
				}
			}
			α Pass( Message&& m )ι->void{
				let id = m.Id();
				Send( move(m) );
				Done( id );
			}
			α Release()ι->bool{//the oldest waiting goes on;  false when none waits.
				if( _waiting.empty() )
					return false;
				Pass( move(_waiting.front()) );
				_waiting.pop_front();
				return true;
			}
			α Turn()ι->void{//a rate's gap has passed:  the next goes, or the rate is open to the next that comes.
				if( Release() )
					Schedule( _interval, [this]{ Turn(); } );
				else
					_open = true;
			}
			bool _rate;
			bool _drop;
			Duration _interval;//a delay's hold, or the gap between a rate's messages.
			bool _open{ true };//a rate's:  the next message goes by.
			std::deque<Message> _waiting;
			std::deque<TimerId> _timers;//a delay's, one per message waiting, oldest first.
		};

		//Sends `first`, a payload, as a message arrives and `second` once `wait` has passed with the trigger armed, each when
		//set;  messages that come while it is armed end here, and with `extend` each starts the wait again, the watchdog:
		//no `first`, a `second` of "no value for 30 s".  A message whose payload is `reset`, or that has a `reset` property,
		//disarms it and sends nothing.  Armed per `topic` with `perTopic`.
		struct Trigger final : INode{
			Trigger( const jobject& props )ε:
				_wait{ Prop::Wait(props, "wait") },
				_extend{ Prop::Flag(props, "extend") },
				_perTopic{ Prop::Flag(props, "perTopic") }{
				if( let v = Prop::Find(props, "first") )
					_first = *v;
				if( let v = Prop::Find(props, "second") )
					_second = *v;
				if( let v = Prop::Find(props, "reset") )
					_reset = *v;
				THROW_IF( !_first && !_second, "a trigger sends a `first`, a `second` or both." );
			}
			α Receive( Message& m )ε->void override{
				string key{ _perTopic ? m.Topic() : sv{} };
				auto p = _armed.find( key );
				if( m.Json.contains("reset") || (_reset && m.Payload()==*_reset) ){
					if( p!=_armed.end() ){
						Cancel( p->second.Timer );
						_armed.erase( p );
					}
					return;
				}
				if( p!=_armed.end() && !_extend )
					return;
				let fresh = p==_armed.end();
				if( fresh )
					p = _armed.try_emplace( key ).first;
				else
					Cancel( p->second.Timer );
				p->second.Last = m.Id();
				p->second.Topic = m.Topic();
				p->second.Timer = Schedule( _wait, [this, key]{ Fire( key ); } );
				if( fresh && _first ){
					m.Json["payload"] = *_first;
					Send( move(m) );
				}
			}
		private:
			struct Arming final{ TimerId Timer{}; MessageId Last{}; string Topic; };
			α Fire( str key )ι->void{
				auto p = _armed.find( key );
				if( p==_armed.end() )
					return;
				let arming = move( p->second );
				_armed.erase( p );
				if( !_second )
					return;
				auto m = Make( arming.Last );
				if( !arming.Topic.empty() )
					m.Json["topic"] = arming.Topic;
				m.Json["payload"] = *_second;
				Send( move(m) );
			}
			Duration _wait;
			bool _extend;
			bool _perTopic;
			optional<jvalue> _first, _second, _reset;
			std::map<string,Arming> _armed;
		};
	}
}
namespace Jde::Opc{
	α Flows::MakeDelay( const jobject& props )ε->up<INode>{ return mu<Delay>( props ); }
	α Flows::MakeTrigger( const jobject& props )ε->up<INode>{ return mu<Trigger>( props ); }
}