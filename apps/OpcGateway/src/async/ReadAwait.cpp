#include "ReadAwait.h"
#include <jde/fwk/utils/collections.h>
#include <jde/ql/types/TableQL.h>
#include <jde/opc/uatypes/Value.h>
#include "../UAClient.h"
#include "../uatypes/Browse.h"

#define let const auto
namespace Jde::Opc::Gateway{
	flat_map<string,UA_AttributeId> _attributes{
    { "invalid", UA_ATTRIBUTEID_INVALID },
//		{ "id", UA_ATTRIBUTEID_NODEID },
		{ "accessLevel", UA_ATTRIBUTEID_ACCESSLEVEL },
		{ "accessLevelEx", UA_ATTRIBUTEID_ACCESSLEVELEX },
		{ "accessRestrictions", UA_ATTRIBUTEID_ACCESSRESTRICTIONS },
		{ "arrayDimensions", UA_ATTRIBUTEID_ARRAYDIMENSIONS },
    { "browse", UA_ATTRIBUTEID_BROWSENAME },
		{ "containsNoLoops", UA_ATTRIBUTEID_CONTAINSNOLOOPS },
    { "dataType", UA_ATTRIBUTEID_DATATYPE },
		{ "dataTypeDefinition", UA_ATTRIBUTEID_DATATYPEDEFINITION },
    { "description", UA_ATTRIBUTEID_DESCRIPTION },
		{ "eventNotifier", UA_ATTRIBUTEID_EVENTNOTIFIER },
    { "executable", UA_ATTRIBUTEID_EXECUTABLE },
		{ "historizing", UA_ATTRIBUTEID_HISTORIZING },
		{ "inverseName", UA_ATTRIBUTEID_INVERSENAME },
		{ "isAbstract", UA_ATTRIBUTEID_ISABSTRACT },
		{ "minimumSamplingInterval", UA_ATTRIBUTEID_MINIMUMSAMPLINGINTERVAL },
    { "name", UA_ATTRIBUTEID_DISPLAYNAME },
		{ "nodeClass", UA_ATTRIBUTEID_NODECLASS },
		{ "rolePermissions", UA_ATTRIBUTEID_ROLEPERMISSIONS },
		{ "userAccessLevel", UA_ATTRIBUTEID_USERACCESSLEVEL },
		{ "userExecutable", UA_ATTRIBUTEID_USEREXECUTABLE },
		{ "userRolePermissions", UA_ATTRIBUTEID_USERROLEPERMISSIONS },
		{ "userWriteMask", UA_ATTRIBUTEID_USERWRITEMASK },
		{ "writeMask", UA_ATTRIBUTEID_WRITEMASK },
		{ "symmetric", UA_ATTRIBUTEID_SYMMETRIC },
    { "value", UA_ATTRIBUTEID_VALUE },
		{ "valueRank", UA_ATTRIBUTEID_VALUERANK },
	};

	Ω attributes( const QL::TableQL& ql )ι->flat_map<UA_NodeClass, vector<UA_AttributeId>>{
		auto y = ReserveMap<UA_NodeClass, vector<UA_AttributeId>>( ql.Columns.size() );
		auto extract = [&]( const QL::TableQL& ql, UA_NodeClass nodeClass ){
			vector<UA_AttributeId> attribs;
			for( let& c : ql.Columns ){
				if( auto attrib = _attributes.find(c.JsonName); attrib != _attributes.end() )
					attribs.emplace_back( attrib->second );
			}
			if( !attribs.empty() )
				 y[nodeClass] = move( attribs );
		};
		extract( ql, UA_NODECLASS_UNSPECIFIED );
		for( auto frag : ql.InlineFragments ){
			let& name = frag.JsonName;
			if( name=="Object" )
				extract( frag, UA_NODECLASS_OBJECT );
			else if( name=="Variable" )
				extract( frag, UA_NODECLASS_VARIABLE );
			else if( name=="Method" )
				extract( frag, UA_NODECLASS_METHOD );
			else if( name=="ObjectType" )
				extract( frag, UA_NODECLASS_OBJECTTYPE );
			else if( name=="VariableType" )
				extract( frag, UA_NODECLASS_VARIABLETYPE );
			else if( name=="RefType" )
				extract( frag, UA_NODECLASS_REFERENCETYPE );
			else if( name=="DataType" )
				extract( frag, UA_NODECLASS_DATATYPE );
			else if( name=="View" )
				extract( frag, UA_NODECLASS_VIEW );
		}
		return y;
	}
	Ω allAttributes( const QL::TableQL& ql )ι->vector<UA_AttributeId>{
		auto y = attributes( ql );
		auto all = y.find( UA_NODECLASS_UNSPECIFIED );
		return all==y.end() ? vector<UA_AttributeId>{} : all->second;
	}
	ReadRequest::ReadRequest( Browse::Response&& browse, QL::TableQL&& ql )ι:
		UA_ReadRequest{}{
		let attribs = attributes( ql );
		if( attribs.empty() )
			return;
		auto all = attribs.find( UA_NODECLASS_UNSPECIFIED );
		_readIds.reserve( (all==attribs.end() ? 0 : all->second.size())*(browse.resultsSize ? browse.results[0].referencesSize : 0) );

		auto add = [&]( const UA_ReferenceDescription& ref, const vector<UA_AttributeId>& attribs ){
			for( let& attrib : attribs )
				Push( ref.nodeId.nodeId, attrib );
		};
		browse.VisitWhile( 0, [&](let& ref){
			if( all!=attribs.end() )
				add( ref, all->second );
			if( auto p = attribs.find(ref.nodeClass); p != attribs.end() )
				add( ref, p->second );
			return true;
		});
		SetNodesToRead();
	}

	ReadRequest::ReadRequest( const vector<NodeId>& ids, const QL::TableQL& ql )ι:
		UA_ReadRequest{}{
		let attribs = allAttributes( ql );
		_readIds.reserve( ids.size()*attribs.size() );
		for( let& nodeId : ids ){
			for( let& attrib : attribs )
				Push( nodeId, attrib );
		}
		SetNodesToRead();
	}
	ReadRequest::ReadRequest( const vector<NodeId>& ids, UA_AttributeId attrib )ι:
		UA_ReadRequest{}{
		_readIds.reserve( ids.size() );
		for( let& nodeId : ids )
			Push( nodeId, attrib );
		SetNodesToRead();
	}
	ReadRequest::ReadRequest( const NodeId& nodeId, std::initializer_list<UA_AttributeId> attribs )ι:
		UA_ReadRequest{}{
		_readIds.reserve( attribs.size() );
		for( let attrib : attribs )
			Push( nodeId, attrib );
		SetNodesToRead();
	}
	ReadRequest::ReadRequest( ReadRequest&& x )ι:
		UA_ReadRequest{ x },
		_readIds{ move(x._readIds) }{
		x.nodesToRead=nullptr; x.nodesToReadSize=0;//x no longer owns the identifiers this points at.
		SetNodesToRead();
	}
	ReadRequest::~ReadRequest(){
		for( auto& id : _readIds )
			UA_ReadValueId_clear( &id );
	}
	α ReadRequest::operator=( ReadRequest&& x )ι->ReadRequest&{
		if( this != &x ){
			for( auto& id : _readIds )
				UA_ReadValueId_clear( &id );
			*( UA_ReadRequest* )this = x;
			_readIds = move( x._readIds );
			x.nodesToRead=nullptr; x.nodesToReadSize=0;
			SetNodesToRead();
		}
		return *this;
	}
	α ReadRequest::Push( const UA_NodeId& nodeId, UA_AttributeId attrib )ι->void{
		auto& id = _readIds.emplace_back( UA_ReadValueId{{}, (UA_UInt32)attrib, UA_STRING_NULL, {0, UA_STRING_NULL}} );
		UA_NodeId_copy( &nodeId, &id.nodeId );//deep: ~ReadRequest clears it, and the source is gone by the time Suspend encodes.
	}

	α ReadRequest::Add( const QL::TableQL& ql, const flat_map<NodeId, jobject>& nodes )ι->void{
		let attribs = allAttributes( ql );
		_readIds.reserve( _readIds.size()+nodes.size()*attribs.size() );
		for( let& [nodeId, _] : nodes ){
			for( let& attrib : attribs )
				Push( nodeId, attrib );
		}
		SetNodesToRead();
	}

	α ReadRequest::AtribString( UA_AttributeId id )->const string&{
		for( let& [k,v] : _attributes )
			if( v==id )
				return k;
		return _attributes.find("invalid")->first;
	}

	ReadResponse::ReadResponse( UA_ReadResponse&& x, ReadRequest&& request )ι:
		UA_ReadResponse{ x },
		Request{ move(request) }{
		UA_ReadResponse_init( &x );
	}
	ReadResponse::ReadResponse( ReadResponse&& x )ι:
		UA_ReadResponse{ x },
		Request{ move(x.Request) }{
		UA_ReadResponse_init( &x );
	}

	α ReadResponse::operator=( ReadResponse&& x )ι->ReadResponse&{
		if( this != &x ){
			UA_ReadResponse_clear( this );
			*( UA_ReadResponse* )this = x;
			Request = move( x.Request );
			UA_ReadResponse_init( &x );
		}
		return *this;
	}

	α ReadResponse::Validate( Handle uahandle, SL sl )ε->void{
		THROW_IFX( responseHeader.serviceResult, UAClientException(responseHeader.serviceResult, uahandle, "UA_Client_Service_read().", sl) );
	}

	α ReadResponse::ToJson( const QL::TableQL& ql )ι->jvalue{
		auto nodeJson = GetJson();
		jarray y;
		for( auto&& [nodeId, j] : nodeJson ){
			if( ql.FindColumn("id") )
				j["id"] = nodeId.ToJson();
			y.push_back( move(j) );
		}
		return ql.IsPlural() ? jvalue{ move(y) } : ( y.size() ? jvalue{move(y[0])} : jobject{} );
	}

	α ReadResponse::SetJson( flat_map<NodeId, jobject>& nodes )ι->void{
		for( uint i=0; i<Request->nodesToReadSize; ++i ){
			UA_ReadValueId& attribReq = Request->nodesToRead[i];
			const NodeId nodeId{ attribReq.nodeId };
			auto nodeIt = nodes.try_emplace( nodeId );
			jobject& j = nodeIt.first->second;
			let attrib = ( UA_AttributeId )attribReq.attributeId;
			//Value::ToJson, not `status ? Variant{status} : Variant{value}`: that collapse discarded the reading for any
			//non-zero status - #15's bug, alive on this path - and serialized a Bad code as a bare number posing as a value.
			//Now Bad → {"sc":…}, a non-zero code with a reading → {"v":…,"sc":…}, and Int64s go out in the Long form
			//the SPA's toValue() was written for.
			j[ReadRequest::AtribString( attrib )] = Value{ move(results[i]) }.ToJson();
		}
	}


	α ReadAwait::Suspend()ι->void{
		_client->PostUA( [this]{//UA submissions must run on the client's strand; `this` outlives suspension (resume only via OnComplete).
			try{
				UACε( UA_Client_sendAsyncReadRequest(*_client, &_request, ReadAwait::OnResponse, this, &_requestId) );
				_client->Process( _requestId, _keepAlive ? "keep-alive" : "read", _keepAlive );
			}
			catch( UAException& e ){
				ResumeExp( move(e) );
			}
		});
	}
	α ReadAwait::OnResponse( UA_Client* /*client*/, void* await, UA_UInt32 /*requestId*/, UA_ReadResponse* rr )ι->void{
		ASSERT( await );
		( (ReadAwait*)await )->OnComplete( rr );
	}
	α ReadAwait::OnComplete( UA_ReadResponse* rr )ι->void{
		_client->ClearRequest( _requestId );
		ReadResponse response{ move(*rr), move(_request) };
		try{
			response.Validate( _client->Handle(), _sl );
			Resume( move(response) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α ReadAwait::await_resume()ε->ReadResponse{
		return Promise() ? TAwait<ReadResponse>::await_resume() : ReadResponse{{}, move(_request)};
	}
}