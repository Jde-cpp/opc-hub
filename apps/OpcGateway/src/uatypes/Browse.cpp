#include "Browse.h"
#include <jde/fwk/process/execution.h>
#include <jde/fwk/utils/collections.h>
#include <jde/ql/types/TableQL.h>
#include <jde/opc/uatypes/BrowseName.h>
#include <jde/opc/uatypes/LocalizedText.h>
#include <jde/opc/uatypes/Value.h>
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	//Each segment `ns~name`, or a bare name in defaultNS.
	Ω toSegments( std::span<const sv> segments, NsIndex defaultNS )ι->vector<BrowseSegment>{
		auto y = Reserve<BrowseSegment>( segments.size() );
		for( let segment : segments ){
			BrowseSegment browse{ defaultNS, string{segment} };
			if( let nsPath = Str::Split(segment, '~'); nsPath.size()>1 ){
				if( let ns = Str::TryTo<NsIndex>(nsPath[0]); ns ){
					browse.Ns = *ns;
					browse.Name = Str::Join( std::span{nsPath}.subspan(1), "~" );
				}
			}
			y.push_back( move(browse) );
		}
		return y;
	}
	UABrowsePath::UABrowsePath( std::span<const sv> segments, NsIndex defaultNS, const NodeId& start )ι:
		UABrowsePath{ toSegments(segments, defaultNS), start }
	{}

	UABrowsePath::UABrowsePath( std::span<const BrowseSegment> segments, const NodeId& start )ι:
		Owned{ UA_BrowsePath{
			{},
			{ segments.size(), (UA_RelativePathElement*)UA_Array_new(segments.size(), &UA_TYPES[UA_TYPES_RELATIVEPATHELEMENT]) }
		} }{
		UA_NodeId_copy( &start, &startingNode );//deep copy - clear() owns it.
		for( size_t i=0; i<segments.size(); ++i ){
			auto elem = &relativePath.elements[i];
			elem->referenceTypeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_HIERARCHICALREFERENCES );
			elem->includeSubtypes = true;
			elem->isInverse = false;
			elem->targetName = { segments[i].Ns, AllocUAString(segments[i].Name) };
		}
	}

namespace Browse{
	Ω onResponse( UA_Client* /*ua*/, void* userdata, RequestId /*requestId*/, UA_BrowseResponse* response )ι->void;
	α FoldersAwait::Suspend()ι->void{
		ASSERT( Promise() );
		Submit( "BrowseRequest", [this]{
			let sc = UA_Client_sendAsyncBrowseRequest( _client->UAPointer(), &_request, onResponse, this, &_requestId );
			DBGT( BrowseTag, "[{}.{}]({})SendBrowseRequest", hex(_client->Handle()), hex(_requestId), hex(sc) );
			return sc;
		});
	}
	Ω onResponse( UA_Client* /*ua*/, void* userdata, RequestId /*requestId*/, UA_BrowseResponse* response )ι->void {
		FoldersAwait& await = *( FoldersAwait* )userdata;
		await.OnComplete( response );
	}
	α FoldersAwait::OnComplete( UA_BrowseResponse* response )ι->void{
		ASSERT( Promise() );
		ClearRequest();
		let sc = response->responseHeader.serviceResult;
		DBGT( BrowseTag, "[{}.{}]({})SendBrowseRequest::Complete", hex(_client->Handle()), hex(_requestId), hex(sc) );
		if( !UA_StatusCode_isBad(sc) ){//a Good_*/Uncertain_* informational code still carries the results.
			if( auto resultSC = !_request.PerNode && response->resultsSize>0 ? response->results[0].statusCode : UA_STATUSCODE_GOOD; UA_StatusCode_isBad(resultSC) ){
				DBGT( BrowseTag, "[{}.{}]({})SendBrowseRequest::Results Error", hex(_client->Handle()), hex(_requestId), hex(resultSC) );
				ResumeExp( UAClientException{resultSC, _client->Handle(), _requestId} );
			}else
				Post<Response>( move(*response), move(_h) );
		}else
			ResumeExp( UAClientException{sc, _client->Handle(), _requestId} );
	}

	flat_map<string, UA_BrowseResultMask> _attributes = {
		{ "none", UA_BROWSERESULTMASK_NONE },
		{ "browse", UA_BROWSERESULTMASK_BROWSENAME },
		{ "isForward", UA_BROWSERESULTMASK_ISFORWARD },
		{ "name", UA_BROWSERESULTMASK_DISPLAYNAME },
		{ "nodeClass", UA_BROWSERESULTMASK_NODECLASS },
		{ "refType", UA_BROWSERESULTMASK_REFERENCETYPEID },
		{ "typeDef", UA_BROWSERESULTMASK_TYPEDEFINITION },
	};

	Request::Request( NodeId&& id, UA_BrowseResultMask mask )ι:
		Owned{ UA_BrowseRequest{ .requestedMaxReferencesPerNode=0, .nodesToBrowseSize=1, .nodesToBrowse=UA_BrowseDescription_new() } }{
		nodesToBrowse[0].nodeId = id.Move(); //not move(id): that slices, and ~NodeId then frees the id the in-flight request points at.
	 	nodesToBrowse[0].resultMask = mask;
	}
	Request::Request( vector<NodeId>&& ids, UA_BrowseResultMask mask )ι:
		Owned{ UA_BrowseRequest{ .requestedMaxReferencesPerNode=0, .nodesToBrowseSize=ids.size(), .nodesToBrowse=(UA_BrowseDescription*)UA_Array_new(ids.size(), &UA_TYPES[UA_TYPES_BROWSEDESCRIPTION]) } }{
		for( uint i=0; i<ids.size(); ++i ){
			nodesToBrowse[i].nodeId = ids[i].Move();//Move(), as above:  a slice would leave ~NodeId freeing the id the in-flight request points at.
			nodesToBrowse[i].resultMask = mask;
		}
	}

	Ω calcMask( const QL::TableQL& ql )ι->UA_BrowseResultMask{
		UA_BrowseResultMask mask = UA_BROWSERESULTMASK_NONE;
		for( let& c : ql.Columns ){
			if( auto attrib = _attributes.find(c.JsonName); attrib!=_attributes.end() )
				mask |= attrib->second;
		}
		return mask;
	}
	Request::Request( NodeId&& id, const QL::TableQL& ql )ι:
		Request( move(id), calcMask(ql) )
	{}
	α Request::Hierarchical( NodeId&& id, UA_BrowseResultMask mask )ι->Request{
		vector<NodeId> ids; ids.push_back( move(id) );//one description setup, shared - so the two overloads cannot drift.
		return Hierarchical( move(ids), mask );
	}
	α Request::Hierarchical( vector<NodeId>&& ids, UA_BrowseResultMask mask )ι->Request{
		Request y{ move(ids), mask };
		y.PerNode = true;//the crawl reads every result - a batch of one after a fallback included.
		for( uint i=0; i<y.nodesToBrowseSize; ++i ){
			auto& d = y.nodesToBrowse[i];
			d.browseDirection = UA_BROWSEDIRECTION_FORWARD;
			d.referenceTypeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_HIERARCHICALREFERENCES );
			d.includeSubtypes = true;
			d.nodeClassMask = UA_NODECLASS_OBJECT | UA_NODECLASS_VARIABLE | UA_NODECLASS_METHOD;
		}
		return y;
	}
	α Request::Parents( NodeId&& id )ι->Request{
		Request y{ move(id), (UA_BrowseResultMask)(UA_BROWSERESULTMASK_BROWSENAME | UA_BROWSERESULTMASK_NODECLASS) };
		auto& d = y.nodesToBrowse[0];
		d.browseDirection = UA_BROWSEDIRECTION_INVERSE;
		d.referenceTypeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_HIERARCHICALREFERENCES );
		d.includeSubtypes = true;
		d.nodeClassMask = UA_NODECLASS_OBJECT | UA_NODECLASS_VARIABLE;//DI/IA hang children under variables too, as the crawl allows.
		return y;
	}
	α Request::Properties( NodeId&& id )ι->Request{
		Request y{ move(id), UA_BROWSERESULTMASK_BROWSENAME };
		auto& d = y.nodesToBrowse[0];
		d.browseDirection = UA_BROWSEDIRECTION_FORWARD;
		d.referenceTypeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_HASPROPERTY );
		d.includeSubtypes = false;
		d.nodeClassMask = UA_NODECLASS_VARIABLE;
		return y;
	}

	α Response::VisitWhile( uint resultsIndex, function<bool(const UA_ReferenceDescription& ref)> f )Ι->bool{
		if( resultsIndex>=resultsSize ){
			ASSERT_DESC( !resultsSize, Ƒ("resultsIndex {} out of range {}.", resultsIndex, resultsSize) );//an index past a *non-empty* response is a caller bug, and still worth flagging.
			return true;
		}
		bool returnedFalse{};
		for( size_t j = 0; j < results[resultsIndex].referencesSize; ++j ){
			returnedFalse = !f( results[resultsIndex].references[j] );
			if( returnedFalse )
				break;
		}
		return !returnedFalse; //Returns false if f ever returns false.
	}
	α Response::SetJson( flat_map<NodeId, jobject>& children, bool addId )Ι->void{
		VisitWhile( 0, [&, addId=addId](const UA_ReferenceDescription& ref){
			jobject o;
			if( Attribs & UA_BROWSERESULTMASK_BROWSENAME )
				o["browse"] = Opc::ToJson( ref.browseName );
			if( Attribs & UA_BROWSERESULTMASK_ISFORWARD )
				o["isForward"] = ref.isForward;
			if( Attribs & UA_BROWSERESULTMASK_DISPLAYNAME )
				o["displayName"] = Opc::ToJson( ref.displayName );
			if( Attribs & UA_BROWSERESULTMASK_NODECLASS )
				o["nodeClass"] = ref.nodeClass;
			if( Attribs & UA_BROWSERESULTMASK_REFERENCETYPEID )
				o["refType"] = Opc::ToJson( ref.referenceTypeId );
			if( Attribs & UA_BROWSERESULTMASK_TYPEDEFINITION )
				o["typeDef"] = Opc::ToJson( ref.typeDefinition );
			NodeId nodeId{ move(ref.nodeId.nodeId) };
			if( addId )
				nodeId.Add( o );
			children.emplace( move(nodeId), o );
			return true;
		} );
	}
}}