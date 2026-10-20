#include "VariableQLAwait.h"
#include "../async/ReadAwait.h"
#include "../async/WriteAwait.h"
#include "../async/ConnectAwait.h"
#include "../async/ValueTypesAwait.h"

#define let const auto

namespace Jde::Opc::Gateway{
	α VariableQLAwait::Execute()ι->TAwait<sp<UAClient>>::Task{
		auto opcId = _mutation.FindPtr<jstring>( "opc" );
		try{
			auto client = co_await ConnectAwait{ string{opcId ? *opcId : sv{}}, _session->SessionId, _session->UserPK, _sl };
			NodeId nodeId{ _mutation.As<>("id") };
			if( _mutation.Type==QL::EMutationQL::Update ){
				let resolved = co_await ValueTypesAwait{ {nodeId}, client, _sl };
				if( let sc = resolved.Statuses[0]; UA_StatusCode_isBad(sc) )
					throw UAException{ sc, "Could not read the node's DataType.", {}, _sl };
				WriteAwait write{ nodeId, Value{_mutation.As<>("value"), resolved.Types[0], _sl}, client };
				co_await Any( write );
				jvalue y;
				if( auto& ql = _mutation.ResultRequest; ql ){
					ReadAwait read{ {{nodeId}, *ql}, client };
					auto response = co_await Any( read );
					y = response.ToJson( *ql );
				}
				Resume( move(y) );
			}
			else
				ResumeExp( Exception("Only update is supported") );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}