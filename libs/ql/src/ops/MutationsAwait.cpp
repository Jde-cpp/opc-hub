#include <jde/ql/ops/MutationsAwait.h>
#include <jde/ql/ops/MutationAwait.h>
#include "../qlInternal.h"

#define let const auto
namespace Jde::QL{
	α MutationsAwait::Execute()ι->TAwait<jvalue>::Task{
		jarray results;
		try{
			for( auto& m : _mutations ){
				LOGSL( ELogLevel::Trace, _sl, ELogTags::QL, "QL: {}", m.ToString() );
				auto resultRequest = m.ResultRequest; // can't move, some mutations may need it
				let returnRaw = m.ReturnRaw;
				let commandName = m.CommandName;
				auto mutationResult = co_await MutationAwait( m, _creds, _ql, _sl );
				if( !resultRequest )
					continue;
				if( IsSystemMutation(commandName) ){//its await shaped the result to the request itself:  merging the arguments in would add them to its lists.
					results.push_back( returnRaw ? move(mutationResult) : jvalue{ jobject{{commandName, move(mutationResult)}} } );
					continue;
				}
				if( auto array = mutationResult.is_array() ? &mutationResult.get_array() : nullptr; array && array->size() )
					mutationResult = Json::AsObject( move((*array)[0]) );
				//The columns the mutation answered as it answered them, the arguments only for the columns it has none of - an insert's {id}
				//beside what was inserted.  Merged in (Combine), a server's read-back took on the arguments' keys and elements:  an
				//ExpandedNodeId written by uri kept that uri though the server's answer had it as an index, and an array the server clamped
				//gained the element that was asked for (reviews-issue/423-clients-writes.md W4).
				auto available = mutationResult.is_object() ? move( mutationResult.get_object() ) : jobject{};
				for( auto&& [key, value] : m.ExtrapolateVariables() ){
					if( !available.contains(key) )
						available.emplace( key, move(value) );
				}
				jobject result;
				auto& returnObject = returnRaw ? result : result[commandName].emplace_object();
				returnObject = resultRequest->TrimColumns( available );
				results.push_back( move(result) );
			}
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
			co_return;
		}
		Resume( results.size()==1 ? move(results[0]) : move(results) );
	}
}