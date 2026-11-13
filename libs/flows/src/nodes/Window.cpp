#include <deque>
#include <jde/flows/Props.h>
#include "Nodes.h"
#include "Operand.h"

#define let const auto

namespace Jde::Opc::Flows{
	namespace{
		constexpr std::array<sv,2> _kinds{ "tumbling", "sliding" };
		struct Figures final{
			α Add( double v, MessageId id )ι->void{
				if( !Count++ )
					First = Min = Max = v;
				Min = std::min( Min, v );
				Max = std::max( Max, v );
				Sum += v;
				Last = v;
				LastId = id;
			}
			α ToJson()Ι->jobject{
				return { {"count", Count}, {"sum", Sum}, {"min", Min}, {"max", Max}, {"mean", Sum/Count}, {"first", First}, {"last", Last}, {"delta", Last-First} };
			}
			uint Count{};
			double Sum{}, Min{}, Max{}, First{}, Last{};
			MessageId LastId{};
		};

		//A window over `property`, a number, by `duration` or by `count`, per `topic` unless `perTopic` is false, its payload
		//{ count, sum, min, max, mean, first, last, delta } and its `_parent` the last message in it.  "tumbling":  by duration
		//the windows are the clock's, each a multiple of the duration from its epoch, and one with a value is sent at its end;
		//by count one is sent with its last value.  "sliding":  each value sends the window that ends with it, the last
		//`duration` or the last `count` values.  A message with a `reset` property empties every window and sends nothing.
		struct Window final : INode{
			Window( const jobject& props )ε:
				_property{ Prop::Text(props, "property", "payload") },
				_sliding{ Prop::Choice(props, "kind", _kinds, 0)==1 },
				_perTopic{ Prop::Flag(props, "perTopic", true) },
				_duration{ Prop::FindWait(props, "duration") },
				_count{ Prop::FindCount(props, "count") }{
				THROW_IF( _duration.has_value()==_count.has_value(), "a window is by `duration` or by `count`, one of the two." );
			}
			α Receive( Message& m )ε->void override{
				if( m.Json.contains("reset") ){
					Cancel( std::exchange(_timer, 0) );
					_end = {};
					_windows.clear();
					return;
				}
				let v = _property.Find( m.Json );
				let x = v ? AsNumber( *v ) : nullopt;
				THROW_IF( !x, "'{}' is not a number to take into a window.", _property.Text() );
				let now = Now();
				string topic{ _perTopic ? m.Topic() : sv{} };
				if( _sliding ){
					auto& values = _windows[topic].Values;
					values.emplace_back( now, *x );
					while( _duration ? values.front().first<=now-*_duration : values.size()>*_count )
						values.pop_front();
					while( values.size()>QueueLimit() )
						values.pop_front();
					Figures figures;
					for( let& [_, value] : values )
						figures.Add( value, m.Id() );
					Emit( topic, figures );
				}
				else if( _count ){
					auto p = _windows.try_emplace( topic ).first;
					p->second.Total.Add( *x, m.Id() );
					if( p->second.Total.Count<*_count )
						return;
					let figures = p->second.Total;
					_windows.erase( p );
					Emit( topic, figures );
				}
				else{
					if( _timer && now>=_end )//the timer is late:  this value is the next window's.
						Close();
					if( !_timer ){
						_end = TimePoint{ (now.time_since_epoch()/ *_duration+1)* *_duration };
						_timer = Schedule( _end, [this]{ Close(); } );
					}
					_windows[topic].Total.Add( *x, m.Id() );
				}
			}
		private:
			struct Held final{ Figures Total; std::deque<std::pair<TimePoint,double>> Values; };
			α Close()ι->void{
				Cancel( std::exchange(_timer, 0) );
				for( let& [topic, held] : std::exchange(_windows, {}) )
					Emit( topic, held.Total );
			}
			α Emit( str topic, const Figures& figures )ι->void{
				auto m = Make( figures.LastId );
				if( !topic.empty() )
					m.Json["topic"] = topic;
				m.Json["payload"] = figures.ToJson();
				Send( move(m) );
			}
			Path _property;
			bool _sliding;
			bool _perTopic;
			optional<Duration> _duration;
			optional<uint> _count;
			std::map<string,Held> _windows;//by topic, so a window's end sends them in order.
			TimePoint _end;//a tumbling duration's.
			TimerId _timer{};
		};
	}
}
namespace Jde::Opc{
	α Flows::MakeWindow( const jobject& props )ε->up<INode>{ return mu<Window>( props ); }
}