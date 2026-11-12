#include "Nodes.h"
#include "Operand.h"
#include "Props.h"

#define let const auto

namespace Jde::Opc::Flows{
	namespace{
		//Report by exception, Node-RED's rbe:  `property`, `payload` by default, passes when it differs from the last one
		//passed, or with `band` when it is a number further from it than the band, an amount or with `percent` a share of
		//the last value;  per `topic` unless `perTopic` is false.  The first passes.  A message without the property ends here.
		struct Filter final : INode{
			Filter( const jobject& props )ε:
				_property{ Prop::Text(props, "property", "payload") },
				_percent{ Prop::Flag(props, "percent") },
				_perTopic{ Prop::Flag(props, "perTopic", true) }{
				if( Prop::Find(props, "band") ){
					_band = Prop::Number( props, "band" );
					THROW_IF( *_band<0, "`band` must not be below zero." );
				}
				THROW_IF( _percent && !_band, "`percent` is of a `band`." );
			}
			α Receive( Message& m )ε->void override{
				let v = _property.Find( m.Json );
				if( !v )
					return;
				let number = _band ? AsNumber( *v ) : nullopt;
				THROW_IF( _band && !number, "'{}' is not a number to hold to a band.", _property.Text() );
				let key = _perTopic ? m.Topic() : sv{};
				auto p = _last.find( key );
				if( p!=_last.end() ){
					if( !_band && Equal(p->second, *v) )
						return;
					if( let last = _band ? AsNumber( p->second ) : nullopt; last && std::abs( *number-*last )<=( _percent ? std::abs(*last)**_band/100 : *_band ) )
						return;
					p->second = *v;
				}
				else
					_last.emplace( key, *v );
				Send( move(m) );
			}
		private:
			Path _property;
			optional<double> _band;
			bool _percent;
			bool _perTopic;
			std::map<string,jvalue,std::less<>> _last;//the last value passed, by topic.
		};

		constexpr std::array<sv,3> _actions{ "scale", "clamp", "wrap" };
		//`property`, a number, from `minIn`..`maxIn` to `minOut`..`maxOut`:  as it falls, clamped to the input range first, or
		//wrapped within it, as `action` says;  `round` to a whole number.
		struct Range final : INode{
			Range( const jobject& props )ε:
				_property{ Prop::Text(props, "property", "payload") },
				_minIn{ Prop::Number(props, "minIn") }, _maxIn{ Prop::Number(props, "maxIn") },
				_minOut{ Prop::Number(props, "minOut") }, _maxOut{ Prop::Number(props, "maxOut") },
				_action{ Prop::Choice(props, "action", _actions, 0) },
				_round{ Prop::Flag(props, "round") }{
				THROW_IF( _minIn==_maxIn, "`minIn` and `maxIn` are the same:  the input range is empty." );
			}
			α Receive( Message& m )ε->void override{
				let v = _property.Find( m.Json );
				auto x = v ? AsNumber( *v ) : nullopt;
				THROW_IF( !x || !std::isfinite(*x), "'{}' is not a number to scale.", _property.Text() );
				let low = std::min( _minIn, _maxIn ); let high = std::max( _minIn, _maxIn );
				if( _action==1 )
					x = std::clamp( *x, low, high );
				else if( _action==2 ){
					let turn = std::fmod( *x-low, high-low );
					x = low+( turn<0 ? turn+high-low : turn );
				}
				let y = ( *x-_minIn )/( _maxIn-_minIn )*( _maxOut-_minOut )+_minOut;
				let whole = std::round( y );
				_property.Set( m.Json, _round && std::abs(whole)<0x1p63 ? jvalue{ (int64_t)whole } : jvalue{ y } );
				Send( move(m) );
			}
		private:
			Path _property;
			double _minIn, _maxIn, _minOut, _maxOut;
			uint _action;
			bool _round;
		};
	}
}
namespace Jde::Opc{
	α Flows::MakeFilter( const jobject& props )ε->up<INode>{ return mu<Filter>( props ); }
	α Flows::MakeRange( const jobject& props )ε->up<INode>{ return mu<Range>( props ); }
}