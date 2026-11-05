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
	//The DisplayNames not yet seen, in as few Reads as the server takes:  all at once, halved for good by each BadTooManyOperations,
	//as ParentPathsAwait's browse is.  The names are decoration - a `references` row's type and type definition - so a Read refused
	//otherwise leaves its ids unnamed, asked again next time, rather than failing the query (reviews-issue/416-address-space.md #8).
	//Two queries racing for the same ids read them twice, which costs a round trip and nothing else.
	α DisplayNameCache::GetAwait::Fetch()ι->VoidTask{
		DisplayNames found;
		uint batch = _missing.size();
		for( uint start=0; start<_missing.size(); ){
			const vector<NodeId> asked{ _missing.begin()+start, _missing.begin()+std::min<uint>(start+batch, _missing.size()) };
			optional<ReadResponse> resp; bool tooMany{};
			try{
				ReadAwait read{ ReadRequest{asked, UA_ATTRIBUTEID_DISPLAYNAME}, _client };
				resp = co_await Any( read );
			}
			catch( const UAException& e ){
				tooMany = e.Code()==UA_STATUSCODE_BADTOOMANYOPERATIONS && asked.size()>1;
				if( !tooMany )
					WARNT( BrowseTag, "[{}]The DisplayNames of {} nodes were refused - left unnamed:  {}", hex(_client->Handle()), asked.size(), e.what() );
			}
			catch( const runtime_error& e ){
				WARNT( BrowseTag, "[{}]The DisplayNames of {} nodes could not be read - left unnamed:  {}", hex(_client->Handle()), asked.size(), e.what() );
			}
			if( tooMany ){//the server's MaxNodesPerRead is below the request:  fewer, and again.
				batch = std::max<uint>( 1, asked.size()/2 );
				DBGT( BrowseTag, "[{}]A read of {} DisplayNames was refused as too many - {} at a time.", hex(_client->Handle()), asked.size(), batch );
				continue;
			}
			for( uint i=0; resp && i<asked.size() && i<resp->resultsSize; ++i ){
				let& r = resp->results[i];
				if( r.hasValue && UA_Variant_hasScalarType(&r.value, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]) )
					found.emplace( asked[i], Opc::ToString(((const UA_LocalizedText*)r.value.data)->text) );
			}
			start += asked.size();
		}
		_cache.Add( move(found) );
		Resume( _cache.Find(_ids) );
	}
}