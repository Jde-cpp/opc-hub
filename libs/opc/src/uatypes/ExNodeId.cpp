#include <jde/opc/uatypes/ExNodeId.h>
#include <jde/opc/uatypes/NodeId.h>

#define let const auto
namespace Jde::Opc{
	ExNodeId::ExNodeId( const NodeId& x )ι:
		ExNodeId{}{
		UA_NodeId_copy( &x, &nodeId );
	}

	//The jvalue overloads of Json::FindDefaultSV/FindNumber take a json *pointer path*, not a member name, so "nsu" never
	//resolved and both extra fields were dropped on every construction from json.  Read them off the object, where the
	//overloads do take a key.  if_object rather than as_object: a bare `5002`/`"tag"` is a legal node id here, which
	//NodeId::FromJson accepts and which as_object would throw on.
	ExNodeId::ExNodeId( const jvalue& j )ε:
		Owned{ UA_ExpandedNodeId{NodeId::FromJson(j), UA_STRING_NULL, 0} }{
		if( let* o = j.if_object(); o ){
			serverIndex = Json::FindNumber<UA_UInt32>( *o, "serverindex" ).value_or( 0 );
			if( let nsu = Json::FindSV(*o, "nsu"); nsu && nsu->size() )
				namespaceUri = AllocUAString( *nsu );
		}
	}

	α ExNodeId::Move()ι->UA_NodeId{
		UA_NodeId y = nodeId;
		UA_NodeId_init( &nodeId );
		return y;
	}

	α ExNodeId::ToJson()Ι->jobject{
		return Opc::ToJson( *this );
	}

	α ExNodeId::ToString()Ι->string{
		UAString s;
		if( let sc = UA_ExpandedNodeId_print(this, &s); sc )
			return serialize( ToJson() );
		return s.ToString();
	}
	Ω addExNodeId( jobject& j, const UA_ExpandedNodeId& x )ι->void{
		if( x.namespaceUri.length )
			j["nsu"] = ToSV(x.namespaceUri);
		if( x.serverIndex )
			j["serverindex"] = x.serverIndex;
		AddNodeId( j, x.nodeId, true );//true: an ExpandedNodeId omits ns 0, the way it omits an empty nsu and server 0.
	}
	α ExNodeId::Add( jobject& j )Ι->void{
		addExNodeId( j, *this );
	}
}
namespace Jde{
	α Opc::ToJson( const UA_ExpandedNodeId& x )ε->jobject{
		jobject j;
		addExNodeId( j, x );
		return j;
	}
}
