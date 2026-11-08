#include "EventFilter.h"
#include <charconv>
#include <open62541/util.h>

#define let const auto
namespace Jde::Opc::Gateway{
	Ω refuse( sv path, sv why, SL sl )ε->Exception{
		return Exception{ sl, ExceptionArgs{EHttpStatus::BadRequest}, "The event field '{}' {}.", path, why };
	}
	Ω segment( sv path, sv text, SL sl )ε->BrowseName{
		let pos = text.find_first_of( ":~" );
		NsIndex ns{};
		if( pos!=sv::npos ){
			let digits = text.substr( 0, pos );
			let [end, ec] = std::from_chars( digits.data(), digits.data()+digits.size(), ns );
			if( ec!=std::errc{} || end!=digits.data()+digits.size() || digits.empty() )
				throw refuse( path, Ƒ("has a namespace index that is no UInt16 - '{}'", digits), sl );
			text = text.substr( pos+1 );
		}
		if( text.empty() )
			throw refuse( path, "has an empty segment", sl );
		return BrowseName{ UA_QualifiedName{ns, AllocUAString(text)} };
	}
	α EventClause::Parse( sv path, SL sl )ε->EventClause{
		EventClause y;
		for( sv rest = path; ; ){
			let slash = rest.find( '/' );
			y.Path.push_back( segment(path, rest.substr(0, slash), sl) );
			if( slash==sv::npos )
				break;
			rest = rest.substr( slash+1 );
		}
		auto operand = y.Operand();
		UAString key;
		let sc = UA_SimpleAttributeOperand_print( &operand, &key );
		UA_SimpleAttributeOperand_clear( &operand );
		if( sc )
			throw refuse( path, Ƒ("cannot be spelled as a select clause - {}", UA_StatusCode_name(sc)), sl );
		y.Key = key.ToString();
		return y;
	}
	α EventClause::Operand()Ι->UA_SimpleAttributeOperand{
		UA_SimpleAttributeOperand y; UA_SimpleAttributeOperand_init( &y );
		y.typeDefinitionId = UA_NODEID_NUMERIC( 0, UA_NS0ID_BASEEVENTTYPE );
		y.attributeId = UA_ATTRIBUTEID_VALUE;
		y.browsePathSize = Path.size();
		y.browsePath = (UA_QualifiedName*)UA_Array_new( Path.size(), &UA_TYPES[UA_TYPES_QUALIFIEDNAME] );
		for( uint i=0; i<Path.size(); ++i )
			UA_QualifiedName_copy( &Path[i], &y.browsePath[i] );
		return y;
	}

	α EventFilter::Base()ι->const vector<EventClause>&{
		static const vector<EventClause> base = []{
			vector<EventClause> y;
			for( sv name : {"EventId", "EventType", "SourceNode", "SourceName", "Time", "ReceiveTime", "Message", "Severity"} )
				y.push_back( EventClause::Parse(name) );
			return y;
		}();
		return base;
	}
	α EventFilter::Of( const vector<string>& select, SL sl )ε->EventFilter{
		EventFilter y{ Base() };
		for( let& path : select )
			y.Add( EventClause::Parse(path, sl) );
		return y;
	}
	α EventFilter::Add( const EventClause& clause )ι->bool{
		if( find(Select, clause)!=Select.end() )
			return false;
		Select.push_back( clause );
		return true;
	}
	α EventFilter::Add( const EventFilter& x )ι->void{
		for( let& clause : x.Select )
			Add( clause );
	}
	α EventFilter::Keys()Ι->flat_set<string>{
		flat_set<string> y;
		for( let& clause : Select )
			y.emplace( clause.Key );
		return y;
	}
	α EventFilter::Set( UA_EventFilter& filter )Ι->void{
		filter.selectClausesSize = Select.size();
		filter.selectClauses = (UA_SimpleAttributeOperand*)UA_Array_new( Select.size(), &UA_TYPES[UA_TYPES_SIMPLEATTRIBUTEOPERAND] );
		for( uint i=0; i<Select.size(); ++i )
			filter.selectClauses[i] = Select[i].Operand();
	}
	α EventFilter::Set( UA_MonitoringParameters& ua )Ι->void{
		UA_EventFilter filter; UA_EventFilter_init( &filter );
		Set( filter );
		UA_ExtensionObject_setValueCopy( &ua.filter, &filter, &UA_TYPES[UA_TYPES_EVENTFILTER] );//a copy:  the request's clear frees it.
		UA_EventFilter_clear( &filter );
	}
}