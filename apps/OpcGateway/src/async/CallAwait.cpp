#include "CallAwait.h"
#include <jde/ql/types/TableQL.h>
#include <jde/opc/uatypes/Value.h>
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	static_assert( sizeof(Variant)==sizeof(UA_Variant), "the request takes _args as a UA_Variant array" );

	CallResult::CallResult( StatusCode sc, const vector<StatusCode>& inputResults )ι{
		statusCode = sc;
		if( inputResults.empty() )
			return;
		inputArgumentResults = (UA_StatusCode*)UA_Array_new( inputResults.size(), &UA_TYPES[UA_TYPES_STATUSCODE] );
		if( !inputArgumentResults )
			return;
		inputArgumentResultsSize = inputResults.size();
		std::ranges::copy( inputResults, inputArgumentResults );
	}
	α CallResult::ToJson( const QL::TableQL* q, const ITypes* types )Ι->jobject{
		jobject y;
		if( !q || q->FindColumn("statusCode") )
			y["statusCode"] = statusCode;
		if( !q || q->FindColumn("inputArgumentResults") ){
			jarray results; results.reserve( inputArgumentResultsSize );
			for( uint i=0; i<inputArgumentResultsSize; ++i )
				results.push_back( inputArgumentResults[i] );
			y["inputArgumentResults"] = move( results );
		}
		if( !q || q->FindColumn("outputArguments") ){
			jarray outputs; outputs.reserve( outputArgumentsSize );
			for( uint i=0; i<outputArgumentsSize; ++i ){
				const UA_DataValue output{ .value=outputArguments[i], .hasValue=true };//an lvalue, so Value copies it:  from a temporary it would take the variant the result still owns.
				outputs.push_back( Value{output}.ToJson(types) );
			}
			y["outputArguments"] = move( outputs );
		}
		return y;
	}

	Ω onResponse( UA_Client*, void* userdata, RequestId /*requestId*/, UA_CallResponse* response )ι->void{
		static_cast<CallAwait*>( userdata )->OnComplete( *response );
	}
	α CallAwait::Suspend()ι->void{
		Submit( "call", [this]{ return UA_Client_call_async( *_client, _object, _method, _args.size(), _args.data(), onResponse, this, &_requestId ); } );
	}
	//open62541 clears the response once this returns, so the result is copied out of it.
	α CallAwait::OnComplete( UA_CallResponse& response )ι->void{
		ClearRequest();
		if( response.responseHeader.serviceResult )
			ResumeExp( UAClientException{response.responseHeader.serviceResult, _client->Handle(), "call", _sl} );
		else if( response.resultsSize!=1 )
			ResumeExp( UAClientException{UA_STATUSCODE_BADUNEXPECTEDERROR, _client->Handle(), Ƒ("call answered {} results for 1 method", response.resultsSize), _sl} );
		else
			Resume( CallResult{response.results[0]} );
	}
}