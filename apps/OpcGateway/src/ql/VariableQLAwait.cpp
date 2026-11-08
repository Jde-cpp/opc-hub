#include "VariableQLAwait.h"
#include "../async/ReadAwait.h"
#include "../async/WriteAwait.h"
#include "../async/ConnectAwait.h"
#include "../async/ValueTypesAwait.h"
#include "../UAClient.h"

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
				//Part 4's "2:4":  the value is the slice (clients spec *Values and writes*, #423).  Text or nothing:  a number was dropped, and the
				//slice written over the whole array.
				let indexRange = _mutation.FindPtr<jvalue>( "indexRange" );
				THROW_IF( indexRange && !indexRange->is_null() && !indexRange->is_string(), "indexRange is a NumericRange's text, \"2:4\" or \"2\" - '{}'.", serialize(*indexRange) );
				WriteAwait write{ nodeId, Value{_mutation.As<>("value"), resolved.Types[0], _sl}, indexRange && indexRange->is_string() ? string{indexRange->get_string()} : string{}, client };
				co_await Any( write );
				jvalue y;
				if( auto& ql = _mutation.ResultRequest; ql ){
					ReadAwait read{ {{nodeId}, *ql}, client };
					auto response = co_await Any( read );
					y = response.ToJson( *ql, client->Types().get() );//the structure written back as fields reads back as them (clients spec *Structures*).
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