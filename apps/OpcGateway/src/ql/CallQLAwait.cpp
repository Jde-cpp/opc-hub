#include "CallQLAwait.h"
#include <jde/opc/uatypes/UAString.h>
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
				let type = i<inputs.size() ? inputs[i].BuiltIn() : nullptr;//an argument past the declared ones, or one of an abstract type, takes the type its json implies - and the server's answer.
				let refuse = [&]( StatusCode sc, string why ){
					DBG( "[{}]call {} on {}:  argument {}{} {}", hex(_client->Handle()), method.ToString(), object.ToString(), i, i<inputs.size() ? Ƒ(" ({})", Opc::ToString(inputs[i].name)) : string{}, why );
					results[i] = sc;
					refused = true;
					values.emplace_back( UA_Variant{} );
				};
				if( type && !Value::Settable(*type) )//a NodeId, a ByteString, a structure holding one (an Argument):  the gateway's limit, which no value typed would get past - not the user's mistake.
					refuse( UA_STATUSCODE_BADNOTSUPPORTED, Ƒ("is a {}, which the gateway cannot send.", type->typeName) );
				else{
					try{
						values.emplace_back( (*jargs)[i], type );
					}
					catch( const std::exception& e ){
						refuse( UA_STATUSCODE_BADTYPEMISMATCH, Ƒ("is no {} - {}", type ? type->typeName : "value", e.what()) );
					}
				}
			}
			const QL::TableQL* request = _mutation.ResultRequest ? &*_mutation.ResultRequest : nullptr;
			if( refused )
				Resume( CallResult{UA_STATUSCODE_BADINVALIDARGUMENT, results}.ToJson(request, _client->Types().get()) );
			else{
				let result = co_await CallAwait{ move(object), move(method), move(values), _client, _sl };
				Resume( result.ToJson(request, _client->Types().get()) );
			}
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}