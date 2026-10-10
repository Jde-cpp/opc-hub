#include "AggregateFunctions.h"
#include <jde/fwk/process/execution.h>
#include "UAClient.h"
#include "uatypes/Browse.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };

	α AggregateFunctions::Part13( sv name )ι->optional<NodeId>{
		//Part 13 §6.3's AggregateFunction objects, by browse name.
		static const flat_map<sv,UA_UInt32> part13{
			{"Interpolative", UA_NS0ID_AGGREGATEFUNCTION_INTERPOLATIVE}, {"Average", UA_NS0ID_AGGREGATEFUNCTION_AVERAGE}, {"TimeAverage", UA_NS0ID_AGGREGATEFUNCTION_TIMEAVERAGE},
			{"TimeAverage2", UA_NS0ID_AGGREGATEFUNCTION_TIMEAVERAGE2}, {"Total", UA_NS0ID_AGGREGATEFUNCTION_TOTAL}, {"Total2", UA_NS0ID_AGGREGATEFUNCTION_TOTAL2},
			{"Minimum", UA_NS0ID_AGGREGATEFUNCTION_MINIMUM}, {"Maximum", UA_NS0ID_AGGREGATEFUNCTION_MAXIMUM}, {"MinimumActualTime", UA_NS0ID_AGGREGATEFUNCTION_MINIMUMACTUALTIME},
			{"MaximumActualTime", UA_NS0ID_AGGREGATEFUNCTION_MAXIMUMACTUALTIME}, {"Range", UA_NS0ID_AGGREGATEFUNCTION_RANGE}, {"Minimum2", UA_NS0ID_AGGREGATEFUNCTION_MINIMUM2},
			{"Maximum2", UA_NS0ID_AGGREGATEFUNCTION_MAXIMUM2}, {"MinimumActualTime2", UA_NS0ID_AGGREGATEFUNCTION_MINIMUMACTUALTIME2}, {"MaximumActualTime2", UA_NS0ID_AGGREGATEFUNCTION_MAXIMUMACTUALTIME2},
			{"Range2", UA_NS0ID_AGGREGATEFUNCTION_RANGE2}, {"Count", UA_NS0ID_AGGREGATEFUNCTION_COUNT}, {"DurationInStateZero", UA_NS0ID_AGGREGATEFUNCTION_DURATIONINSTATEZERO},
			{"DurationInStateNonZero", UA_NS0ID_AGGREGATEFUNCTION_DURATIONINSTATENONZERO}, {"NumberOfTransitions", UA_NS0ID_AGGREGATEFUNCTION_NUMBEROFTRANSITIONS}, {"Start", UA_NS0ID_AGGREGATEFUNCTION_START},
			{"End", UA_NS0ID_AGGREGATEFUNCTION_END}, {"Delta", UA_NS0ID_AGGREGATEFUNCTION_DELTA}, {"StartBound", UA_NS0ID_AGGREGATEFUNCTION_STARTBOUND},
			{"EndBound", UA_NS0ID_AGGREGATEFUNCTION_ENDBOUND}, {"DeltaBounds", UA_NS0ID_AGGREGATEFUNCTION_DELTABOUNDS}, {"DurationGood", UA_NS0ID_AGGREGATEFUNCTION_DURATIONGOOD},
			{"DurationBad", UA_NS0ID_AGGREGATEFUNCTION_DURATIONBAD}, {"PercentGood", UA_NS0ID_AGGREGATEFUNCTION_PERCENTGOOD}, {"PercentBad", UA_NS0ID_AGGREGATEFUNCTION_PERCENTBAD},
			{"WorstQuality", UA_NS0ID_AGGREGATEFUNCTION_WORSTQUALITY}, {"WorstQuality2", UA_NS0ID_AGGREGATEFUNCTION_WORSTQUALITY2}, {"AnnotationCount", UA_NS0ID_AGGREGATEFUNCTION_ANNOTATIONCOUNT},
			{"StandardDeviationSample", UA_NS0ID_AGGREGATEFUNCTION_STANDARDDEVIATIONSAMPLE}, {"VarianceSample", UA_NS0ID_AGGREGATEFUNCTION_VARIANCESAMPLE},
			{"StandardDeviationPopulation", UA_NS0ID_AGGREGATEFUNCTION_STANDARDDEVIATIONPOPULATION}, {"VariancePopulation", UA_NS0ID_AGGREGATEFUNCTION_VARIANCEPOPULATION} };
		let p = part13.find( name );
		return p==part13.end() ? optional<NodeId>{} : optional<NodeId>{ NodeId{0, p->second} };
	}

	α AggregateFunctions::GetAwait::await_ready()ι->bool{
		if( auto p = _cache.Find(); p ){
			_result = move( p );
			return true;
		}
		return false;
	}
	α AggregateFunctions::GetAwait::Suspend()ι->void{
		_cache.Start( move(_client), this );
	}

	α AggregateFunctions::Start( sp<UAClient>&& client, GetAwait* waiter )ι->void{
		bool launch{}; Ptr ready;
		{
			ul _{ _mutex };
			if( _folder )
				ready = _folder;//a Finish raced in between await_ready and here.
			else{
				launch = !_fetching;//first request:  own the browse.
				_fetching = true;
				_waiters.push_back( waiter );//join the in-flight browse.
			}
		}
		if( launch )
			Fetch( move(client) );//fire & forget:  the task keeps the client (and so this cache) alive until Finish.
		else if( ready )
			Post( [waiter, f=move(ready)]() mutable { waiter->Resume(move(f)); } );//never resume inside await_suspend.
	}

	α AggregateFunctions::Finish( Ptr folder, up<Exception> error )ι->void{
		vector<GetAwait*> waiters;
		{
			ul _{ _mutex };
			waiters = move( _waiters );
			_fetching = false;
			if( !error )
				_folder = folder;//a failed browse caches nothing:  the next request browses again.
		}
		for( auto* waiter : waiters ){//outside the lock:  Resume may run the awaiter to completion inline - it is the last use of the waiter.
			if( error )
				waiter->ResumeExp( Exception{*error} );
			else
				waiter->Resume( Ptr{folder} );
		}
	}

	//The folder's objects, forward hierarchical references with the browse name of each:  Organizes in the standard
	//nodeset and OpcServer, whatever another server uses.
	α AggregateFunctions::Fetch( sp<UAClient> client )ι->VoidTask{
		Ptr folder; up<Exception> error;
		try{
			Browse::FoldersAwait browse{ Browse::Request::Hierarchical(NodeId{0, UA_NS0ID_HISTORYSERVERCAPABILITIES_AGGREGATEFUNCTIONS}, UA_BROWSERESULTMASK_BROWSENAME), client };
			auto refs = co_await Any( browse );
			THROW_IF( refs.resultsSize!=1, "Browsing AggregateFunctions returned {} results for 1 node.", refs.resultsSize );
			let sc = refs.results[0].statusCode;
			if( UA_StatusCode_isBad(sc) && sc!=UA_STATUSCODE_BADNODEIDUNKNOWN )
				throw UAClientException{ (StatusCode)sc, client->Handle(), "browse HistoryServerCapabilities/AggregateFunctions" };
			auto y = ms<Folder>();
			if( !UA_StatusCode_isBad(sc) ){
				refs.VisitWhile( 0, [&]( const UA_ReferenceDescription& ref ){
					y->try_emplace( Opc::ToString(ref.browseName.name), NodeId{ref.nodeId.nodeId} );
					return true;
				} );
				INFO( "[{}]HistoryServerCapabilities/AggregateFunctions lists {} aggregates.", hex(client->Handle()), y->size() );
			}
			else//a server without the folder lists nothing, which is kept like any listing.
				INFO( "[{}]The server has no HistoryServerCapabilities/AggregateFunctions, so it lists no aggregates.", hex(client->Handle()) );
			folder = move( y );
		}
		catch( runtime_error& e ){
			if( auto p = dynamic_cast<Exception*>(&e); p )
				error = p->Move();
			else
				error = mu<Exception>( move(e) );
		}
		Finish( move(folder), move(error) );
	}
}