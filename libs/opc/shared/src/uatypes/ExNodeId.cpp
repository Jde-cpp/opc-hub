#include <jde/opc/uatypes/ExNodeId.h>
#include <charconv>
#include <jde/opc/uatypes/NodeId.h>

#define let const auto
namespace Jde::Opc{
	//UA_ExpandedNodeId_print percent-escapes a URI, and UA_ExpandedNodeId_parse undoes it only where it maps the URI to an index.
	Ω unescape( UA_String& uri )ι->StatusCode{
		let s = ToSV( uri );
		if( s.find('%')==sv::npos )
			return UA_STATUSCODE_GOOD;
		string y;
		y.reserve( s.size() );
		for( uint i=0; i<s.size(); ++i ){
			if( s[i]!='%' ){
				y += s[i];
				continue;
			}
			uint8_t c{};
			if( i+2>=s.size() || std::from_chars(s.data()+i+1, s.data()+i+3, c, 16).ptr!=s.data()+i+3 )
				return UA_STATUSCODE_BADDECODINGERROR;
			y += (char)c;
			i += 2;
		}
		UA_String_clear( &uri );
		uri = AllocUAString( y );
		return UA_STATUSCODE_GOOD;
	}

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
	α ExNodeId::Parse( sv text )ι->std::expected<ExNodeId,StatusCode>{
		ExNodeId y;
		auto sc = UA_ExpandedNodeId_parse( &y, ToUV(text) );
		if( !sc && y.namespaceUri.data && !y.namespaceUri.length )//ns 0's node otherwise, where every reader tests the length.
			sc = UA_STATUSCODE_BADDECODINGERROR;
		if( !sc )
			sc = unescape( y.namespaceUri );
		if( sc )
			return std::unexpected( sc );
		return y;
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
