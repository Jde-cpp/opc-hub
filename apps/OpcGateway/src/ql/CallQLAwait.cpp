#include "CallQLAwait.h"
#include <jde/opc/uatypes/UAString.h>
#include "../MethodArguments.h"
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };

	α CallQLAwait::Execute()ι->TAwait<CallResult>::Task{
		try{
			NodeId object{ _mutation.As<>("object") }, method{ _mutation.As<>("method") };
			let jargs = _mutation.FindPtr<jarray>( "args" );
			let arguments = co_await _client->Methods().Get( _client, method, _sl );
			let& inputs = arguments->Inputs;
			let count = jargs ? jargs->size() : 0;
			vector<Variant> values; values.reserve( count );
			vector<StatusCode> results( count, UA_STATUSCODE_GOOD );
			bool refused{};
			for( uint i=0; i<count; ++i ){
				let type = i<inputs.size() ? inputs[i].BuiltIn() : nullptr;//an argument past the declared ones, or one of a type of another namespace (a structure is 8a's), takes the type its json implies - and the server's answer.
				try{
					values.emplace_back( (*jargs)[i], type );
				}
				catch( const std::exception& e ){
					DBG( "[{}]call {} on {}:  argument {}{} is no {} - {}", hex(_client->Handle()), method.ToString(), object.ToString(), i, i<inputs.size() ? Ƒ(" ({})", Opc::ToString(inputs[i].name)) : string{}, type ? type->typeName : "value", e.what() );
					results[i] = UA_STATUSCODE_BADTYPEMISMATCH;
					refused = true;
					values.emplace_back( UA_Variant{} );
				}
			}
			const QL::TableQL* request = _mutation.ResultRequest ? &*_mutation.ResultRequest : nullptr;
			if( refused )
				Resume( CallResult{UA_STATUSCODE_BADINVALIDARGUMENT, results}.ToJson(request) );
			else{
				let result = co_await CallAwait{ move(object), move(method), move(values), _client, _sl };
				Resume( result.ToJson(request) );
			}
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}