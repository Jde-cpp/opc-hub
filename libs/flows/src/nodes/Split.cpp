#include <jde/flows/Props.h>
#include "Nodes.h"

#define let const auto

namespace Jde::Opc::Flows{
	namespace{
		constexpr ELogTags _tags{ ELogTags::App };

		//A payload that is an array or an object into a message per element or member, each the original under a new `_id` with
		//the element as its payload and Node-RED's `parts`, { id, index, count, type, key }, `id` the original's `_id`.  Any
		//other payload passes as it is.
		struct Split final : INode{
			α Receive( Message& m )ε->void override{
				if( !m.Payload().is_array() && !m.Payload().is_object() ){
					Send( move(m) );
					return;
				}
				let payload = std::exchange( m.Json["payload"], nullptr );//out of the message, so a part's clone does not copy the whole.
				let count = payload.is_array() ? payload.get_array().size() : payload.get_object().size();
				uint index{};
				let part = [&]( const jvalue& element, optional<sv> key ){
					auto y = Clone( m );
					y.Json["payload"] = element;
					jobject parts{ {"id", m.Id()}, {"index", index++}, {"count", count}, {"type", key ? "object" : "array"} };
					if( key )
						parts["key"] = *key;
					y.Json["parts"] = move( parts );
					Send( move(y) );
				};
				if( payload.is_array() ){
					for( let& element : payload.get_array() )
						part( element, nullopt );
				}
				else{
					for( let& member : payload.get_object() )
						part( member.value(), member.key() );
				}
			}
		};

		constexpr std::array<sv,3> _modes{ "auto", "array", "object" };
		//A sequence back into one message, made here with the last one in as its `_parent` and its topic.  "auto":  by `parts`,
		//a split's sequence into the array or the object it was, when the last of its count is in.  "array":  the payloads in
		//arrival order, once `count` are in.  "object":  each payload under its `key`, `topic` by default, once `count` keys
		//are in.  Whatever is in goes on `timeout`, counted from a sequence's first message;  the two built here need one or
		//the other.  The sequences in progress are bounded as a queue is, the oldest abandoned.
		struct Join final : INode{
			Join( const jobject& props )ε:
				_mode{ Prop::Choice(props, "mode", _modes, 0) },
				_key{ Prop::Text(props, "key", "topic") },
				_count{ Prop::FindCount(props, "count") },
				_timeout{ Prop::FindWait(props, "timeout") }{
				THROW_IF( _mode!=0 && !_count && !_timeout, "a join into an array or an object needs a `count` or a `timeout`." );
			}
			α Receive( Message& m )ε->void override{
				string id;
				uint expected{ _count.value_or(0) };
				const jobject* parts{};
				if( _mode==0 ){
					parts = Json::FindObject( m.Json, "parts" );
					let partsId = parts ? parts->if_contains( "id" ) : nullptr;
					THROW_IF( !partsId, "the message has no `parts` to join by." );
					id = serialize( *partsId );
					expected = Json::FindNumber<uint>( *parts, "count" ).value_or( 0 );
				}
				//The checks before the sequence, so a message refused leaves none behind.
				auto p = _sequences.find( id );
				let object = p==_sequences.end() ? _mode==2 || (parts && Json::FindDefaultSV(*parts, "type")=="object") : p->second.Value.is_object();
				const jvalue* key{};
				uint index{};
				if( object ){
					key = parts ? parts->if_contains( "key" ) : _key.Find( m.Json );
					THROW_IF( !key || !key->is_string(), "the message has no text to join it under." );
				}
				else{
					let size = p==_sequences.end() ? 0 : p->second.Value.get_array().size();
					index = parts ? Json::FindNumber<uint>( *parts, "index" ).value_or( size ) : size;
					THROW_IF( index>=QueueLimit(), "part {} is past what a join holds.", index );
				}
				if( p==_sequences.end() ){
					p = _sequences.try_emplace( id ).first;
					p->second.Order = ++_order;
					if( _timeout )
						p->second.Timer = Schedule( *_timeout, [this, id]{ Flush( id ); } );
					if( object )
						p->second.Value.emplace_object();
				}
				auto& sequence = p->second;
				auto payload = m.Payload();
				if( object ){
					auto& members = sequence.Value.get_object();
					members[key->get_string()] = move( payload );
					sequence.In = _mode==0 ? sequence.In+1 : members.size();
				}
				else{
					auto& items = sequence.Value.get_array();
					if( index>=items.size() )
						items.resize( index+1 );
					items[index] = move( payload );
					++sequence.In;
				}
				sequence.Last = m.Id();
				sequence.Topic = m.Topic();
				if( expected && sequence.In>=expected )
					Flush( id );
				else if( _sequences.size()>QueueLimit() ){
					let oldest = std::ranges::min_element( _sequences, {}, []( let& s ){ return s.second.Order; } );
					WARN( "[{}]A sequence was abandoned with {} in:  more are in progress than a queue holds.", Def().Label(), oldest->second.In );
					Cancel( oldest->second.Timer );
					_sequences.erase( oldest );
				}
			}
		private:
			struct Sequence final{ jvalue Value{ jarray{} }; uint In{}; uint Order{}; MessageId Last{}; string Topic; TimerId Timer{}; };
			α Flush( str id )ι->void{
				auto p = _sequences.find( id );
				if( p==_sequences.end() )
					return;
				auto sequence = move( p->second );
				_sequences.erase( p );
				Cancel( sequence.Timer );
				auto m = Make( sequence.Last );
				if( !sequence.Topic.empty() )
					m.Json["topic"] = sequence.Topic;
				m.Json["payload"] = move( sequence.Value );
				Send( move(m) );
			}
			uint _mode;
			Path _key;
			optional<uint> _count;
			optional<Duration> _timeout;
			std::map<string,Sequence> _sequences;
			uint _order{};
		};
	}
}
namespace Jde::Opc{
	α Flows::MakeSplit( const jobject& )ε->up<INode>{ return mu<Split>(); }
	α Flows::MakeJoin( const jobject& props )ε->up<INode>{ return mu<Join>( props ); }
}