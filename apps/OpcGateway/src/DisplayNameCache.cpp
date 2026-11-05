#include "DisplayNameCache.h"
#include "UAClient.h"
#include "async/ReadAwait.h"

#define let const auto
namespace Jde::Opc::Gateway{
	α DisplayNameCache::Find( const vector<NodeId>& ids, vector<NodeId>* missing )Ι->DisplayNames{
		DisplayNames y;
		rl _{ _mutex };
		for( let& id : ids ){
			if( auto p = _names.find(id); p!=_names.end() )
				y.emplace( id, p->second );
			else if( missing )
				missing->push_back( id );
		}
		return y;
	}
	α DisplayNameCache::Add( DisplayNames&& names )ι->void{
		ul _{ _mutex };
		for( auto&& [id, name] : names )
			_names.insert_or_assign( id, move(name) );
	}
	α DisplayNameCache::GetAwait::await_ready()ι->bool{
		_missing.clear();
		auto found = _cache.Find( _ids, &_missing );
		if( _missing.size() )
			return false;
		_result = move( found );
		return true;
	}
	//One Read of the DisplayNames not yet seen, whatever their count:  two queries racing for the same ids read them twice, which
	//costs a round trip and nothing else.
	α DisplayNameCache::GetAwait::Fetch()ι->VoidTask{
		try{
			ReadAwait read{ ReadRequest{_missing, UA_ATTRIBUTEID_DISPLAYNAME}, _client };
			auto resp = co_await Any( read );
			DisplayNames found;
			for( uint i=0; i<_missing.size() && i<resp.resultsSize; ++i ){
				let& r = resp.results[i];
				if( r.hasValue && UA_Variant_hasScalarType(&r.value, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]) )
					found.emplace( _missing[i], Opc::ToString(((const UA_LocalizedText*)r.value.data)->text) );
			}
			_cache.Add( move(found) );
			auto y = _cache.Find( _ids );
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}