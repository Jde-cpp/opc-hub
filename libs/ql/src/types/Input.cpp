#include "jde/fwk/usings.h"
#include <jde/ql/types/Input.h>

#define let const auto

namespace Jde::QL{
	α Input::Filter()Ι->const QL::Filter&{
		if( _filter )
			return *_filter;
		auto addFilters = []( jvalue&& value )ε->vector<FilterValue> {
			vector<FilterValue> columnFilters;
			if( value.is_string() || value.is_number() || value.is_null() || value.is_bool() ) //( id: 42 ) or ( schemaName:"opc.default" ) or ( deleted: null )
				columnFilters.emplace_back( DB::EOperator::Equal, move(value) );
			else if( value.is_object() ){ //criteria:{in:[null,"ns=4;i=6012"]}
				for( let& [joperator,opValue] : value.as_object() ){
					let oprtr{ ToQLOperator(joperator) };
					columnFilters.emplace_back( oprtr, move(opValue) );
				}
			}
			else if( value.is_array() ) //( id: [1,2,3] ) or ( name: ["charlie","bob"] )
				columnFilters.emplace_back( DB::EOperator::In, move(value) );
			else
				THROW( "Invalid filter value type '{}'.", to_string(value.kind()) );
			return columnFilters;
		};
		_filter = QL::Filter{};
		auto args = ExtrapolateVariables();
		for( auto& [jsonColumnName,value] : args )
			_filter->ColumnFilters.emplace( jsonColumnName, addFilters(move(value)) );
		return *_filter;
	}

	//One walk over Args for both callers below:  every `\b$name` marker - a member, an array element, at any depth - is handed to
	//`variable( name )` and its answer takes the marker's place; everything else is copied as is.
	template<class F> Ω substitute( const jobject& o, const F& variable )ι->jobject{
		auto value = [&]( const jvalue& v, auto& self )ι->jvalue {
			if( v.is_string() && v.get_string().starts_with(Input::Escape) )
				return variable( sv{v.get_string()}.substr(Input::Escape.size()) );
			if( v.is_object() )
				return substitute( v.get_object(), variable );
			if( let a = v.if_array(); a ){
				jarray y;
				for( let& item : *a )
					y.emplace_back( self(item, self) );
				return y;
			}
			return v;
		};
		jobject y;
		for( let& [key, v] : o )
			y.emplace( key, value(v, value) );
		return y;
	}
	α Input::ExtrapolateVariables()Ι->jobject{
		return substitute( Args, [this]( sv name )ι->jvalue {
			let p = Variables ? Variables->if_contains(name) : nullptr;
			return p ? *p : jvalue{};
		});
	}
	//#36: an unbound $name extrapolated to json null, and null is a value - `users(slug:$targt)` became `slug is null` and
	//returned 0 rows with nothing said, while `updateGroup(name:$typo)` wrote one.  The name itself is only knowable before the
	//substitution, so the check is here rather than at the predicate.  Variables may be null on a hand-built Input: then nothing
	//is bound.
	α Input::UnboundVariables()Ι->vector<string>{
		vector<string> y;
		substitute( Args, [&]( sv name )ι->jvalue{ if( !Variables || !Variables->if_contains(name) ) y.emplace_back( name ); return {}; } );
		return y;
	}
	α Input::CheckVariables( SL sl )Ε->void{
		let unbound = UnboundVariables();
		if( unbound.size() )//the caller's mistake.
			throw Exception{ sl, {ELogTags::QL, 0, EHttpStatus::BadRequest}, "Could not find variable{} '{}' in variables: {}", unbound.size()==1 ? "" : "s", Str::Join(unbound, "', '"), Variables ? serialize(*Variables) : string{"{}"} };
	}
	α Input::GetKey( SL sl )Ε->DB::Key{
		let y = FindKey();
		THROW_IFSL( !y, "Could not find id or slug in mutation  query: {}, variables: {}", serialize(Args), serialize(*Variables) );
		return *y;
	}
	α Input::FindKey()Ι->optional<DB::Key>{
		optional<DB::Key> y;
		if( let id = FindId<uint>(); id )
			y = DB::Key{ *id };
		else if( let slug = FindPtr<jstring>("slug"); slug )
			y = DB::Key{ string{*slug} };
		return y;
	}
	α Input::OrderByJson()Ι->const vector<std::pair<string,bool>>&{	//[column,asc]
		if( _orderBy )
			return *_orderBy;
		_orderBy = vector<std::pair<string,bool>>{};
		let orderBy = FindPtr<jvalue>( "orderBy" );
		if( !orderBy )
			return *_orderBy;
		Json::Visit( *orderBy, [&](const jvalue& v){
			if( v.is_string() )
				_orderBy->emplace_back( string{v.get_string()}, true );
			else if( auto o = v.is_object() ? v.get_object() : jobject{}; !o.empty() ){
				if( let active = o.if_contains("active"), direction = o.if_contains("direction"); active && direction ){
					let descending = direction->if_string() && *direction->if_string()=="desc";
					if( let name = active->if_string(); name )
						_orderBy->emplace_back( string{*name}, !descending );
					else
						DBGT( ELogTags::QL, "orderBy active='{}' is not a column name - ignored.", serialize(*active) );
				}
				else{ //orderBy:{"name","desc"}
					for( let& [key,value] : o )
						_orderBy->emplace_back( key, value.is_string() && value.get_string()!="desc" );
				}
			}
		} );
		return *_orderBy;
	}
	α Input::ArgString()Ι->string{
		if( Args.empty() )
			return {};
		string argStr{ '('};
		for( let& [key, value] : Args ){
			auto addArg = [&key, &argStr]( const jvalue& v ){
				if( v.is_string() )
					argStr += Ƒ( "{}: \"{}\", ", key, (sv)v.get_string() );
				else
					argStr += Ƒ( "{}: {}, ", key, serialize(v) );
			};
			if( let vname = VariableName(value); vname.size() )
				addArg( Variables->if_contains(vname) ? Variables->at(vname) : value );
			else
				addArg( value );
		}
		argStr.resize( argStr.size()-2 ); //each arg appends ", " - overwriting only back() left the comma: "(id: 42,)".
		argStr += ')';
		return argStr;
	}
}