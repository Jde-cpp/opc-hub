#include <jde/fwk/log/logTags.h>
#include <jde/fwk/str.h>
#define let const auto

namespace Jde{
	constexpr sv _noneName{ "none" };//ELogTags::None, which no table names.
	vector<TagName> _libraryTags;//AddTags', in bit order:  every one above FwkTags'.
	Ω tagNames()ι->array<std::span<const TagName>,2>{ return { FwkTags, _libraryTags }; }//every named bit, in bit order.
	Ω toTag( sv name )ι->ELogTags{
		for( let names : tagNames() ){
			if( let named = std::ranges::find(names, name, &TagName::Name); named!=names.end() )
				return named->Tag;
		}
		return ELogTags::None;
	}
	α Logging::AddTags( std::span<const TagName> tags )ε->void{
		for( let& tag : tags ){
			for( let names : tagNames() ){
				for( let& named : names ){
					if( !empty(named.Tag & tag.Tag) || named.Name==tag.Name )
						throw std::logic_error{ Ƒ("log tag '{}' ({:x}) is already registered as '{}' ({:x})", tag.Name, underlying(tag.Tag), named.Name, underlying(named.Tag)) };
				}
			}
		}
		_libraryTags.insert( _libraryTags.end(), tags.begin(), tags.end() );
		std::ranges::sort( _libraryTags, {}, &TagName::Tag );
	}
	α Logging::Tags( bool user )ι->flat_map<string,uint>{
		flat_map<string,uint> y;
		for( let names : tagNames() ){
			for( let& named : names )
				y.emplace( string{named.Name}, underlying(named.Tag) );
		}
		if( user ){
			//named by ToName, not by hand: "socketClientReadSub" was no more a tag name than ["socket","client","read"]
			//was - ToLogTags knows neither - so the catalogue offered the ui rows it could not then save.  It also has to
			//be the *same* spelling the logSetting/instanceTagLevel answers carry, or the ui shows one tag as two rows.
			for( let tags : CompositeTags )
				y.emplace( ToName(tags), underlying(tags) );
		}
		return y;
	}
}

α Jde::ToValue( ELogTags tags )ι->jvalue{
	if( tags==ELogTags::None )
		return jvalue( jstring{_noneName} );//parenthesized: jvalue{x} prefers value's initializer_list constructor, which wraps x in an array.
	jarray tagStrings;
	for( let names : tagNames() ){
		for( let& named : names ){
			if( !empty(tags & named.Tag) )
				tagStrings.push_back( jstring{named.Name} );
		}
	}
	return tagStrings.size()==1
		? tagStrings.front()//the one tag, bare, where an earlier version spelled every single-tag value "none".
		: tagStrings.empty()
			? jvalue{}
			: tagStrings;
}
α Jde::ToName( ELogTags tags )ι->string{
	if( tags==ELogTags::None )
		return string{ _noneName };
	string y;
	for( let names : tagNames() ){
		for( let& named : names ){
			if( empty(tags & named.Tag) )
				continue;
			if( !y.empty() )
				y += TagSeparator;
			y += named.Name;
		}
	}
	return y;
}

α Jde::ToLogTags( sv name )ι->ELogTags{
	if( name=="default" )
		return ELogTags::None;
	ELogTags y{};
	for( let& subName : Str::Split(name, TagSeparator) ){
		let tag = toTag( subName );
		if( empty(tag) )
			WARNT( ELogTags::Settings, "Unknown tag '{}'", subName );
		y |= tag;
	}
	return y;
}
α Jde::ToLogTags( jvalue v )ι->ELogTags{
	if( let s = v.try_as_string(); s )
		return ToLogTags( (sv)*s );
	else if( let arr = v.try_as_array(); arr ){
		ELogTags y{};
		for( let& item : *arr ){
			if( let s = item.try_as_string(); s ){
				y |= ToLogTags( (sv)*s );
			}
			else
				WARNT( ELogTags::Settings, "Expected string in tags array but got {}", to_string(item.kind()) );
		}
		return y;
	}
	else{
		WARNT( ELogTags::Settings, "Expected string or array for tags but got {}", to_string(v.kind()) );
		return ELogTags::None;
	}
}