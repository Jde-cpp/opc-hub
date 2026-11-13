#include <jde/flows/Message.h>

#define let const auto

namespace Jde::Opc::Flows{
	MessageIds::MessageIds( TimePoint start )ι:
		_next{ ((MessageId)std::max<int64_t>(duration_cast<seconds>(start.time_since_epoch()).count(), 0)<<32)+1 }
	{}

	Path::Path( sv text )ε:_text{ text }{
		let refuse = [&]( sv why ){ THROW( "'{}' is not a property's path:  {}.", text, why ); };
		for( uint i=0; i<text.size(); ){
			if( text[i]=='[' ){
				let end = text.find( ']', i );
				let digits = end==sv::npos ? sv{} : text.substr( i+1, end-i-1 );
				uint index{};
				let [p, ec] = std::from_chars( digits.data(), digits.data()+digits.size(), index );
				if( _steps.empty() || digits.empty() || ec!=std::errc{} || p!=digits.data()+digits.size() )
					refuse( "an index is a number in brackets after a name" );
				_steps.emplace_back( index );
				i = end+1;
			}
			else{
				if( !_steps.empty() && text[i++]!='.' )
					refuse( "a name follows a '.'" );
				let end = std::min( text.find_first_of(".[", i), text.size() );
				if( end==i )
					refuse( "a name is missing" );
				_steps.emplace_back( string{text.substr(i, end-i)} );
				i = end;
			}
		}
		if( _steps.empty() )
			refuse( "it is empty" );
	}

	Ω step( const jvalue* v, const std::variant<string,uint>& step )ι->const jvalue*{
		if( let name = std::get_if<string>(&step) ){
			let o = v->if_object();
			return o ? o->if_contains( *name ) : nullptr;
		}
		let a = v->if_array();
		let i = std::get<uint>( step );
		return a && i<a->size() ? &(*a)[i] : nullptr;
	}
	α Path::Find( const jobject& o )Ι->const jvalue*{
		auto v = o.if_contains( Root() );
		for( uint i=1; v && i<_steps.size(); ++i )
			v = step( v, _steps[i] );
		return v;
	}
	α Path::Parent( const jobject& o )Ι->const jvalue*{
		auto v = _steps.size()>1 ? o.if_contains( Root() ) : nullptr;
		for( uint i=1; v && i+1<_steps.size(); ++i )
			v = step( v, _steps[i] );
		return v;
	}
	α Path::Set( jobject& o, jvalue value )Ε->void{
		jvalue* v = &o[Root()];
		for( uint i=1; i<_steps.size(); ++i ){
			if( let name = std::get_if<string>(&_steps[i]) ){
				THROW_IF( !v->is_object() && !v->is_null(), "'{}' cannot be set:  it passes through a value that is not an object.", _text );
				auto& parent = v->is_object() ? v->get_object() : v->emplace_object();
				v = &parent[*name];
			}
			else{
				let index = std::get<uint>( _steps[i] );
				let a = v->if_array();
				THROW_IF( !a || index>a->size(), "'{}' cannot be set:  index {} is past its array's end.", _text, index );
				if( index==a->size() )
					a->emplace_back( nullptr );
				v = &(*a)[index];
			}
		}
		*v = move( value );
	}
	α Path::Erase( jobject& o )Ι->bool{
		if( _steps.size()==1 )
			return o.erase( Root() );
		let parent = const_cast<jvalue*>( Parent(o) );
		if( !parent )
			return false;
		if( let name = std::get_if<string>(&_steps.back()) ){
			let holder = parent->if_object();
			return holder && holder->erase( *name );
		}
		let a = parent->if_array();
		let i = std::get<uint>( _steps.back() );
		if( !a || i>=a->size() )
			return false;
		a->erase( a->begin()+i );
		return true;
	}

	Ω id( const jobject& o, sv key )ι->MessageId{
		let v = o.if_contains( key );
		boost::system::error_code ec;
		let y = v ? v->to_number<MessageId>( ec ) : 0;
		return ec ? 0 : y;
	}
	α Message::Id()Ι->MessageId{ return id( Json, "_id" ); }
	α Message::Parent()Ι->MessageId{ return id( Json, "_parent" ); }
	α Message::Topic()Ι->sv{ return Json::FindDefaultSV( Json, "topic" ); }
	α Message::Payload()Ι->const jvalue&{
		static const jvalue null;
		let v = Json.if_contains( "payload" );
		return v ? *v : null;
	}
	α Message::SetDataChange( sv connection, sv nodeId, const Value& value, const ITypes* types )ι->void{
		Json["payload"] = ToPayload( value, types );
		Json["node"] = jobject{ {"connection", connection}, {"id", nodeId} };
		Json["status"] = value.status;
		if( value.hasSourceTimestamp )
			Json["sourceTime"] = ToIsoString( UADateTime{value.sourceTimestamp}.Time() );
		if( value.hasServerTimestamp )
			Json["serverTime"] = ToIsoString( UADateTime{value.serverTimestamp}.Time() );
	}

	//Value::ToJson's Longs, `{high, low, unsigned}`, at any depth, back to the integers they stand for.
	Ω integers( jvalue& v )ι->void{
		if( let o = v.if_object() ){
			if( o->size()==3 && o->contains("high") && o->contains("low") && o->contains("unsigned") ){
				try{
					v = FromLong( *o );
				}
				catch( const Exception& )
				{}
				return;
			}
			for( auto& member : *o )
				integers( member.value() );
		}
		else if( let a = v.if_array() ){
			for( auto& element : *a )
				integers( element );
		}
	}
}
namespace Jde::Opc{
	α Flows::ToPayload( const Value& value, const ITypes* types )ι->jvalue{
		if( UA_StatusCode_isBad(value.status) || value.IsEmpty() )
			return {};
		auto y = value.ValueJson( types );
		integers( y );
		return y;
	}
}