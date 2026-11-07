#pragma once
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Variant.h>
#include "UAAwait.h"

namespace Jde::QL{ struct TableQL; }
namespace Jde::Opc::Gateway{
	struct UAClient;
	//One method call's result:  the call's status, a status per input argument where the server refused one of them - open62541
	//answers them for BadInvalidArgument alone, none for a call it made - and the outputs.
	struct CallResult final : Owned<UA_CallMethodResult, UA_TYPES_CALLMETHODRESULT>{
		using Owned::Owned;
		//A result made before any call:  `sc` with `inputResults`, one per argument sent - the shape the server answers a mistyped
		//argument with, for one the gateway could not type (CallQLAwait).
		CallResult( StatusCode sc, const vector<StatusCode>& inputResults )ι;
		//{statusCode, inputArgumentResults:[…], outputArguments:[…]} - the statuses as numbers, the outputs as Value::ToJson gives them
		//(clients spec *Methods*), a structure by `types`, the connection's registry - the members `q` names, every one without a query.
		α ToJson( const QL::TableQL* q, const ITypes* types=nullptr )Ι->jobject;
	};
	//UA_Client_call_async for one method on one object, on the client's strand:  the request borrows the ids and the arguments until
	//the submit has encoded it.  The result is the server's whatever its status - a refused call is an answer, with its argument
	//results - and only a failure of the service itself, or of the submit, resumes with an exception.
	struct CallAwait final : UAAwait<CallResult>{
		CallAwait( NodeId object, NodeId method, vector<Variant>&& args, sp<UAClient> c, SRCE )ι:UAAwait{ move(c), sl }, _object{move(object)}, _method{move(method)}, _args{move(args)}{}
		α Suspend()ι->void override;
		α OnComplete( UA_CallResponse& response )ι->void;
	private:
		NodeId _object, _method;
		vector<Variant> _args;//the request's input array, layout-compatible with UA_Variant[].
	};
}