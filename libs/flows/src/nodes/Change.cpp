#include "Nodes.h"
#include "Operand.h"
#include "Props.h"

#define let const auto

namespace Jde::Opc::Flows{
	namespace{
		enum class EOp : uint8{ Set, Delete, Move };
		constexpr std::array<sv,3> _ops{ "set", "delete", "move" };

		//A property of the message, or a key of a context.
		struct Target final{
			Target( const jobject& rule, sv key, sv typeKey )ε:
				Scope{ ToScope(rule, typeKey, EScope::Msg) }{
				if( Scope!=EScope::Msg ){
					Key = Prop::Text( rule, key );
					return;
				}
				Property.emplace( Prop::Text(rule, key) );
				THROW_IF( Property->Root()=="_id" || Property->Root()=="_parent", "`{}` is the engine's:  no rule changes a message's `_id` or `_parent`.", Property->Text() );
			}
			EScope Scope;
			optional<Path> Property;
			string Key;
		};
		struct Rule final{
			EOp Op;
			Target What;
			optional<Operand> Value;
			optional<Target> To;
		};

		//`rules`, in order:  { t: "set", p, pt, to, tot } puts `to`, a literal or what `tot` names, at `p`;  "delete" takes `p`
		//away;  "move" takes it to the property or the key `to` names, a rename.  `pt` and `tot` are "msg", "flow" or "global".
		//A value the message or the context lacks takes the target away.
		struct Change final : INode{
			Change( const jobject& props )ε{
				for( let& item : Prop::Rules(props) ){
					let& rule = item.get_object();
					auto& y = _rules.emplace_back( (EOp)Prop::Choice(rule, "t", _ops), Target{rule, "p", "pt"} );
					if( y.Op==EOp::Set )
						y.Value.emplace( rule, "to", "tot" );
					else if( y.Op==EOp::Move )
						y.To.emplace( rule, "to", "tot" );
				}
			}
			α Receive( Message& m )ε->void override{
				for( let& rule : _rules ){
					jvalue scratch;
					if( rule.Op==EOp::Delete )
						Write( rule.What, m, {} );
					else if( rule.Op==EOp::Set ){
						let v = rule.Value->Find( m, FlowContext(), Global(), scratch );
						Write( rule.What, m, v ? jvalue{ *v } : jvalue{} );
					}
					else{
						auto v = Read( rule.What, m );
						Write( rule.What, m, {} );
						Write( *rule.To, m, move(v) );
					}
				}
				Send( move(m) );
			}
		private:
			α Read( const Target& target, const Message& m )ι->jvalue{
				if( target.Scope==EScope::Global )
					return Global().Get( target.Key );
				let v = target.Scope==EScope::Msg ? target.Property->Find( m.Json ) : FlowContext().Get( target.Key );
				return v ? *v : jvalue{};
			}
			α Write( const Target& target, Message& m, jvalue&& v )ε->void{//null takes it away.
				if( target.Scope==EScope::Flow )
					FlowContext().Set( target.Key, move(v) );
				else if( target.Scope==EScope::Global )
					Global().Set( target.Key, move(v) );
				else if( v.is_null() )
					target.Property->Erase( m.Json );
				else
					target.Property->Set( m.Json, move(v) );
			}
			vector<Rule> _rules;
		};
	}
}
namespace Jde::Opc{
	α Flows::MakeChange( const jobject& props )ε->up<INode>{ return mu<Change>( props ); }
}