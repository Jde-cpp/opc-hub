#include "NodeIndex.h"
#include <jde/fwk/process/execution.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/str.h>
#include "UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	//The session or channel went under the crawl:  nothing more in this crawl will browse.
	Ω sessionLost( StatusCode sc )ι->bool{
		switch( sc ){
			case UA_STATUSCODE_BADSESSIONIDINVALID: case UA_STATUSCODE_BADSESSIONCLOSED: case UA_STATUSCODE_BADSERVERHALTED:
			case UA_STATUSCODE_BADCONNECTIONCLOSED: case UA_STATUSCODE_BADSERVERNOTCONNECTED: case UA_STATUSCODE_BADSECURECHANNELCLOSED:
				return true;
			default: return false;
		}
	}
	//The server could not serve the node this time, not "cannot browse it".
	Ω transient( StatusCode sc )ι->bool{
		switch( sc ){
			case UA_STATUSCODE_BADNOCONTINUATIONPOINTS: case UA_STATUSCODE_BADTOOMANYOPERATIONS: case UA_STATUSCODE_BADTIMEOUT: case UA_STATUSCODE_BADRESOURCEUNAVAILABLE:
				return true;
			default: return false;
		}
	}
	constexpr std::chrono::seconds _failedHold{ 30 };
	//A silent copy for each waiter - the original logs, once.  Keeps the type:  Exception{e} slices a UAException's status off,
	//and that status is what the SPA's notice shows.  ExternalException's copy ctor is already NoLog.
	Ω copy( const Exception& e )ι->up<Exception>{
		if( auto ua = dynamic_cast<const UAException*>(&e); ua )
			return mu<UAException>( *ua );
		return mu<Exception>( e.What(), ExceptionArgs{ELogLevel::NoLog, e.Tags, e.Code(), e.HttpStatus()}, e.Source() );
	}
	α NodeIndex::ReadyAwait::await_ready()ι->bool{
		sl _{ _index._mutex };
		return _index._state==EState::Ready && !_refresh;
	}
	α NodeIndex::ReadyAwait::Suspend()ι->void{
		_index.StartLocked( move(_client), _refresh, this );
	}

	α NodeIndex::StartLocked( sp<UAClient>&& client, bool refresh, AnyVoidAwait* waiter )ι->void{
		bool launch{}, ready{};
		sp<Exception> failed;
		{
			ul _{ _mutex };
			if( _state==EState::Crawling )
				{}//join the in-flight crawl - a refresh included:  its result is fresh enough.
			else if( _state==EState::Failed && !refresh && _error && steady_clock::now()-_failedAt<_failedHold )
				failed = copy( *_error );//answer the recent failure - `refresh` bypasses it.
			else if( _state!=EState::Ready || refresh ){
				_state = EState::Crawling;
				launch = true;
			}
			else
				ready = true;
			if( waiter && !ready && !failed )
				_waiters.push_back( waiter );
		}
		if( launch )
			Crawl( move(client) );//fire & forget:  the task keeps the client (and so this index) alive until Finish.
		if( waiter && ready )
			Post( [waiter]{ waiter->Resume(); } );//a Finish raced in between await_ready and here - never resume inside await_suspend.
		else if( waiter && failed )
			Post( [waiter, failed]{ waiter->ResumeExp( move(*copy(*failed)) ); } );//never inside await_suspend, as above.
	}

	α NodeIndex::Crawl( sp<UAClient> client )ι->TAwait<Browse::Response>::Task{
		vector<Entry> entries;
		bool truncated{};//the index is incomplete - reported through Truncated().
		uint refused{};//nodes whose own browse result was bad - skipped, not fatal.
		StatusCode firstRefusal{};
		bool stop{};//abandon the crawl entirely.  Only maxNodes does that:  a continuation point caps one folder, and the
		//children that folder *did* return still have to be visited and queued, so the two cannot share a flag.
		up<Exception> error;
		let start = std::chrono::steady_clock::now();
		//value_or defaults mirror config/Opc.Gateway.jsonnet - keep them in lockstep.
		let maxDepth = Settings::FindNumber<uint8>( "/gateway/search/maxDepth" ).value_or( 12 );
		let maxNodes = Settings::FindNumber<uint>( "/gateway/search/maxNodes" ).value_or( 25000 );
		let includeServer = Settings::FindBool( "/gateway/search/includeServer" ).value_or( false );
		let browseBatch = std::max<uint>( 1, Settings::FindNumber<uint>("/gateway/search/browseBatch").value_or(64) );
		let skipNames = Settings::FindArray( "/gateway/search/skipBrowseNames" ) ? Settings::FindStringArray( "/gateway/search/skipBrowseNames" ) : vector<string>{ "_Hints" };
		try{
			let defaultNs = client->DefaultBrowseNs();
			struct Pending{ NodeId Id; string Path; uint8 Depth; bool Retried{}; };
			std::deque<Pending> pending;
			pending.push_back( {NodeId::ObjectsFolder(), {}, 0} );
			flat_set<NodeId> seen; seen.emplace( NodeId::ObjectsFolder() );//cycles:  hierarchical references may still reach a node twice.
			bool warnedSlash{};
			constexpr UA_BrowseResultMask mask{ UA_BROWSERESULTMASK_BROWSENAME|UA_BROWSERESULTMASK_DISPLAYNAME|UA_BROWSERESULTMASK_NODECLASS };
			//Browse a whole BFS level per round trip rather than one folder at a time:  a Browse request carries a
			//BrowseDescription per node and answers results[i] for nodesToBrowse[i], which is what VisitWhile's
			//resultsIndex was always for.  On a deep server this is the difference between one round trip per folder and
			//one per level.
			uint batch = browseBatch;
			while( pending.size() && !stop ){
				let count = std::min<uint>( batch, pending.size() );
				vector<Pending> level; level.reserve( count );
				vector<NodeId> ids; ids.reserve( count );
				for( uint i=0; i<count; ++i ){
					level.push_back( move(pending.front()) ); pending.pop_front();
					ids.push_back( NodeId{level.back().Id} );
				}
				optional<Browse::Response> response;
				bool tooMany{};
				try{
					response = co_await Browse::FoldersAwait{ Browse::Request::Hierarchical(move(ids), mask), client };
				}
				catch( UAException& e ){
					//The server's MaxNodesPerBrowse is below our batch.  Nothing advertises it on this path, so learn it
					//from the refusal rather than guessing:  put the level back and go one at a time from here on.
					if( e.Code()!=UA_STATUSCODE_BADTOOMANYOPERATIONS || batch==1 )
						throw;
					tooMany = true;
				}
				if( tooMany ){
					WARNT( BrowseTag, "[{}]search index: browse of {} nodes refused with BadTooManyOperations - falling back to one node per request.", hex(client->Handle()), count );
					batch = 1;
					for( auto p = level.rbegin(); p!=level.rend(); ++p )
						pending.push_front( move(*p) );
					continue;
				}
				if( response->resultsSize<level.size() ){//the spec owes one result per description:  a short answer is a server fault.
					Logging::LogOnce( SRCE_CUR, BrowseTag, "[{}]search index: a browse of {} nodes answered {} results.  Short answers log at Debug from here on.", hex(client->Handle()), level.size(), response->resultsSize );
					DBGT( BrowseTag, "[{}]search index: a browse of {} nodes answered {} results.", hex(client->Handle()), level.size(), response->resultsSize );
					batch = std::max<uint>( 1, batch/2 );
					for( uint i=response->resultsSize; i<level.size(); ++i ){
						if( !level[i].Retried )//once more, as a transient refusal is.
							pending.push_back( {NodeId{level[i].Id}, level[i].Path, level[i].Depth, true} );
						else{
							truncated = true;
							if( !refused++ )
								firstRefusal = UA_STATUSCODE_BADUNEXPECTEDERROR;
						}
					}
				}
				for( uint r=0; r<level.size() && r<response->resultsSize && !stop; ++r ){
					let& parent = level[r];
					let rsc = response->results[r].statusCode;
					if( rsc && !UA_StatusCode_isBad(rsc) )//Good_*/Uncertain_* informational:  the references came with it, so index them.
						TRACET( BrowseTag, "[{}]search index: '{}' ({}) answered ({}){} - indexing its references.", hex(client->Handle()), parent.Path, parent.Id.ToString(), hex(rsc), UAException::Message(rsc) );
					if( UA_StatusCode_isBad(rsc) ){//e.g. BadNodeIdUnknown for _Hints templates under a filtered browse.
						if( sessionLost(rsc) )//fail the crawl, so the next search starts over on a live session.
							throw UAException{ rsc, Ƒ("'{}' lost its session while indexing '{}'.", client->Slug(), parent.Path) };
						if( transient(rsc) && !parent.Retried ){//once more, at the back of the queue, in smaller batches.
							DBGT( BrowseTag, "[{}]search index: retrying '{}' ({}) - ({}){}.", hex(client->Handle()), parent.Path, parent.Id.ToString(), hex(rsc), UAException::Message(rsc) );
							batch = std::max<uint>( 1, batch/2 );
							pending.push_back( {NodeId{parent.Id}, parent.Path, parent.Depth, true} );
							continue;
						}
						truncated = true;//the node and everything under it is missing.
						if( !refused++ )
							firstRefusal = rsc;
						Logging::LogOnce( SRCE_CUR, BrowseTag, "[{}]search index: skipping '{}' ({}) - ({}){}.  Refusals log at Debug from here on.", hex(client->Handle()), parent.Path, parent.Id.ToString(), hex(rsc), UAException::Message(rsc) );
						DBGT( BrowseTag, "[{}]search index: skipping '{}' ({}) - ({}){}.", hex(client->Handle()), parent.Path, parent.Id.ToString(), hex(rsc), UAException::Message(rsc) );
						continue;
					}
					if( response->results[r].continuationPoint.length ){//v1 does not BrowseNext:  the folder is indexed up to the server's per-browse cap.
						WARNT( BrowseTag, "[{}]search index: '{}' returned a continuation point - children beyond the server's limit are not indexed.", hex(client->Handle()), parent.Path );
						truncated = true;//not `stop`:  the references this browse did return are still ours to index.
					}
					response->VisitWhile( r, [&]( const UA_ReferenceDescription& ref ){
						NodeId id{ ref.nodeId.nodeId };
						if( seen.find(id)!=seen.end() )
							return true;
						if( !includeServer && ref.nodeId.nodeId.namespaceIndex==0 && ref.nodeId.nodeId.identifierType==UA_NODEIDTYPE_NUMERIC && ref.nodeId.nodeId.identifier.numeric==UA_NS0ID_SERVER )
							return true;//the Server object's diagnostics/capabilities subtree is hundreds of nodes nobody searches for.
						string browse{ ToSV(ref.browseName.name) };
						if( std::ranges::find(skipNames, browse)!=skipNames.end() )
							return true;//not indexed, not descended - e.g. _Hints address templates.
						if( browse.find('/')!=string::npos ){//unroutable:  the node url is '/'-delimited browse segments.
							if( !warnedSlash ){
								WARNT( BrowseTag, "[{}]search index: skipping browse names containing '/' ('{}' under '{}').", hex(client->Handle()), browse, parent.Path );
								warnedSlash = true;
							}
							return true;
						}
						let segment = ref.browseName.namespaceIndex==defaultNs ? browse : Ƒ( "{}~{}", ref.browseName.namespaceIndex, browse );
						string path = parent.Path.empty() ? segment : Ƒ( "{}/{}", parent.Path, segment );
						let depth = (uint8)( parent.Depth+1 );
						if( (ref.nodeClass==UA_NODECLASS_OBJECT || ref.nodeClass==UA_NODECLASS_VARIABLE) && depth<maxDepth )//DI/IA hang children under variables too.
							pending.push_back( {NodeId{id}, path, depth} );
						seen.emplace( NodeId{id} );
						Entry e{ move(id), move(path), string{ToSV(ref.displayName.text)}, browse, {}, Str::ToLower(browse), ref.browseName.namespaceIndex, ref.nodeClass, depth };
						e.NameLower = Str::ToLower( e.Name );
						entries.push_back( move(e) );
						if( entries.size()>=maxNodes ){
							WARNT( BrowseTag, "[{}]search index: stopped at /gateway/search/maxNodes={}.", hex(client->Handle()), maxNodes );
							truncated = stop = true;
						}
						return !stop;
					} );
				}
			}
			if( entries.empty() && refused )//Objects itself refused:  a Ready empty index would answer [] until a refresh, and the notice would never show.
				throw UAException{ firstRefusal, Ƒ("'{}' refused browsing Objects.", client->Slug()) };
			let ms = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now()-start ).count();
			INFOT( BrowseTag, "[{}]Indexed {} nodes under Objects for '{}' (maxDepth={}) in {}ms{}{}.", hex(client->Handle()), entries.size(), client->Slug(), maxDepth, ms, truncated ? " - truncated" : "", refused ? Ƒ(" - {} node(s) refused browse", refused) : string{} );
		}
		catch( runtime_error& e ){
			WARNT( BrowseTag, "[{}]search index for '{}' failed: {}", hex(client->Handle()), client->Slug(), e.what() );
			if( auto p = dynamic_cast<Exception*>(&e); p )
				error = p->Move();
			else
				error = mu<Exception>( move(e) );
		}
		Finish( move(entries), truncated, move(error) );
	}

	α NodeIndex::Finish( vector<Entry>&& entries, bool truncated, up<Exception> error )ι->void{
		vector<AnyVoidAwait*> waiters;
		{
			ul _{ _mutex };
			if( error ){
				_state = EState::Failed;//searches within _failedHold get this error;  the first after it crawls from scratch.
				_error = copy( *error );
				_failedAt = steady_clock::now();
			}
			else{
				_error.reset();
				_entries = move( entries );
				_truncated = truncated;
				_state = EState::Ready;
			}
			waiters = move( _waiters );
			_waiters.clear();
		}
		for( auto* waiter : waiters ){//outside the lock:  Resume may run the awaiter to completion (and back into Search) inline - it is the last use of the waiter.
			if( error )
				waiter->ResumeExp( move(*copy(*error)) );
			else
				waiter->Resume();
		}
	}

	α NodeIndex::Search( sv text, uint limit )Ι->vector<Entry>{
		vector<const Entry*> hits;
		vector<Entry> y;
		sl _{ _mutex };
		vector<uint8> ranks; ranks.reserve( _entries.size() );
		for( let& e : _entries ){
			uint8 rank = e.NameLower.starts_with(text) ? 0 : e.BrowseLower.starts_with(text) ? 1 : e.NameLower.find(text)!=string::npos || e.BrowseLower.find(text)!=string::npos ? 2 : 3;
			ranks.push_back( rank );
			if( rank<3 )
				hits.push_back( &e );
		}
		auto rankOf = [&]( const Entry* e ){ return ranks[e-&_entries[0]]; };
		std::ranges::sort( hits, [&]( const Entry* a, const Entry* b ){
			let ra = rankOf(a), rb = rankOf(b);
			return ra!=rb ? ra<rb : a->Depth!=b->Depth ? a->Depth<b->Depth : a->NameLower<b->NameLower;
		} );
		if( limit && hits.size()>limit )
			hits.resize( limit );
		y.reserve( hits.size() );
		for( auto* hit : hits ){
			y.push_back( *hit );
			y.back().Rank = rankOf( hit );
		}
		return y;
	}
}
