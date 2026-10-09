#include "VariableQLAwait.h"
#include "../async/ConnectAwait.h" //!important
#include "../async/ValueTypesAwait.h"

#define let const auto

namespace Jde::Opc::Gateway{
	α VariableQLAwait::Execute()ι->TAwait<sp<UAClient>>::Task{
		auto opcId = _mutation.FindPtr<jstring>( "opc" );
		try{
			_client = co_await ConnectAwait{ string{opcId ? *opcId : sv{}}, _session->SessionId, _session->UserPK, _sl };
			_nodeId = NodeId{ _mutation.As<>("id") };
			if( _mutation.Type==QL::EMutationQL::Update ){
				let resolved = co_await ValueTypesAwait{ {_nodeId}, _client, _sl };
				if( let sc = resolved.Statuses[0]; UA_StatusCode_isBad(sc) )
					throw UAException{ sc, "Could not read the node's DataType.", {}, _sl };
				Write( Value{_mutation.As<>("value"), resolved.Types[0], _sl} );
			}
			else
				ResumeExp( Exception("Only update is supported") );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α VariableQLAwait::Write( Value&& value )ι->TAwait<WriteResponse>::Task{
		try{
			auto response = co_await WriteAwait{ _nodeId, move(value), _client };
			if( _mutation.ResultRequest.has_value() )
				Read( move(*_mutation.ResultRequest) );
			else
				Resume( jvalue{} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α VariableQLAwait::Read( QL::TableQL&& ql )ι->TAwait<ReadResponse>::Task{
		try{
			auto response = co_await ReadAwait{ {{_nodeId}, ql}, _client };
			Resume( response.ToJson(ql) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}