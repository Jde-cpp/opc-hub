#include <regex>
#include "Nodes.h"
#include "Operand.h"
#include "Props.h"

#define let const auto

namespace Jde::Opc::Flows{
	namespace{
		enum class EOp : uint8{ Eq, Neq, Lt, Lte, Gt, Gte, Between, Contains, Regex, True, False, Null, NotNull, Empty, NotEmpty, IsType, Else };
		constexpr std::array<sv,17> _ops{ "eq", "neq", "lt", "lte", "gt", "gte", "btwn", "cont", "regex", "true", "false", "null", "nnull", "empty", "nempty", "istype", "else" };//Node-RED's names.
		constexpr std::array<sv,6> _types{ "string", "number", "boolean", "array", "object", "null" };

		struct Rule final{
			EOp Op;
			optional<Operand> Value, Upper;
			optional<std::regex> Pattern;
			uint Type{};
		};

		//`rules` over `property`, `payload` by default:  { t, v, vt } with `v2`, `v2t` for btwn and `case` for regex, `vt` naming
		//where `v` is when it is not a literal.  The message goes to the output of each rule that matches, or with `checkall`
		//false to the first's;  an `else` matches when none before it did.  Nothing coerces:  text is never a number.
		struct Switch final : INode{
			Switch( const jobject& props )ε:
				_property{ Prop::Text(props, "property", "payload") },
				_all{ Prop::Flag(props, "checkall", true) }{
				for( let& item : Prop::Rules(props) ){
					let& rule = item.get_object();
					auto& y = _rules.emplace_back( (EOp)Prop::Choice(rule, "t", _ops) );
					if( y.Op<=EOp::Contains )
						y.Value.emplace( rule, "v", "vt" );
					if( y.Op==EOp::Between )
						y.Upper.emplace( rule, "v2", "v2t" );
					else if( y.Op==EOp::IsType )
						y.Type = Prop::Choice( rule, "v", _types );
					else if( y.Op==EOp::Regex ){
						let pattern = Prop::Text( rule, "v" );
						try{
							y.Pattern.emplace( pattern, Prop::Flag(rule, "case") ? std::regex::ECMAScript | std::regex::icase : std::regex::ECMAScript );
						}
						catch( const std::regex_error& e ){
							THROW( "'{}' is not a regular expression:  {}", pattern, e.what() );
						}
					}
				}
			}
			α Receive( Message& m )ε->void override{
				let v = _property.Find( m.Json );
				vector<uint> ports;
				for( uint i=0; i<_rules.size(); ++i ){
					let& rule = _rules[i];
					if( rule.Op==EOp::Else ? ports.empty() : Matches(rule, v, m) ){
						ports.push_back( i );
						if( !_all )
							break;
					}
				}
				SendEach( move(m), ports );
			}
		private:
			α Matches( const Rule& rule, const jvalue* v, const Message& m )ε->bool{
				jvalue scratch, scratch2;
				let x = rule.Value ? rule.Value->Find( m, FlowContext(), Global(), scratch ) : nullptr;
				let order = v && x ? Order( *v, *x ) : nullopt;
				switch( rule.Op ){
				case EOp::Eq: return v && x && Equal( *v, *x );
				case EOp::Neq: return !( v && x && Equal(*v, *x) );
				case EOp::Lt: return order && *order<0;
				case EOp::Lte: return order && *order<=0;
				case EOp::Gt: return order && *order>0;
				case EOp::Gte: return order && *order>=0;
				case EOp::Between:{
					let upper = rule.Upper->Find( m, FlowContext(), Global(), scratch2 );
					let high = v && upper ? Order( *v, *upper ) : nullopt;
					return order && high && ( (*order>=0 && *high<=0) || (*order<=0 && *high>=0) );
				}
				case EOp::Contains:
					if( v && x && v->is_string() && x->is_string() )
						return sv{ v->get_string() }.contains( sv{x->get_string()} );
					return v && x && v->is_array() && std::ranges::any_of( v->get_array(), [x]( let& element ){ return Equal(element, *x); } );
				case EOp::Regex:{
					if( !v || !v->is_primitive() || v->is_null() )
						return false;
					let text = v->is_string() ? string{ v->get_string() } : serialize( *v );
					return std::regex_search( text, *rule.Pattern );
				}
				case EOp::True: return v && v->is_bool() && v->get_bool();
				case EOp::False: return v && v->is_bool() && !v->get_bool();
				case EOp::Null: return !v || v->is_null();
				case EOp::NotNull: return v && !v->is_null();
				case EOp::Empty: return v && Size( *v )==0;
				case EOp::NotEmpty: return v && Size( *v ).value_or( 0 )>0;
				case EOp::IsType: return v && TypeOf( *v )==rule.Type;
				default: return false;
				}
			}
			Ω Size( const jvalue& v )ι->optional<uint>{
				if( let s = v.if_string() )
					return s->size();
				if( let a = v.if_array() )
					return a->size();
				let o = v.if_object();
				return o ? optional<uint>{ o->size() } : nullopt;
			}
			Ω TypeOf( const jvalue& v )ι->uint{//its index in _types.
				return v.is_string() ? 0 : v.is_number() ? 1 : v.is_bool() ? 2 : v.is_array() ? 3 : v.is_object() ? 4 : 5;
			}
			Path _property;
			bool _all;
			vector<Rule> _rules;
		};
	}
}
namespace Jde::Opc{
	α Flows::MakeSwitch( const jobject& props )ε->up<INode>{ return mu<Switch>( props ); }
}