#include "Browse.h"
#include <jde/fwk/process/execution.h>
#include <jde/ql/types/TableQL.h>
#include <jde/opc/uatypes/BrowseName.h>
#include <jde/opc/uatypes/LocalizedText.h>
#include <jde/opc/uatypes/Value.h>
#include "../UAClient.h"
#include "../async/DataTypeAttribAwait.h"
#include "../async/ReadValueAwait.h"
#include "../async/SessionAwait.h"

#define let const auto
namespace Jde::Opc::Gateway{
	UABrowsePath::UABrowsePath( std::span<const sv> segments, NsIndex defaultNS )ι:
		UA_BrowsePath{
			UA_NODEID_NUMERIC( 0, UA_NS0ID_OBJECTSFOLDER ),
			{ segments.size(), (UA_RelativePathElement*)UA_Array_new(segments.size(), &UA_TYPES[UA_TYPES_RELATIVEPATHELEMENT]) }
		}{
		for( size_t i=0; i<segments.size(); ++i ){
			auto elem = &relativePath.elements[i];
			elem->referenceTypeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_ORGANIZES );
			auto ns = defaultNS;
			string path{ segments[i] };
			if( let nsPath = Str::Split(segments[i], '~'); nsPath.size()>1 ){
				auto specifiedNs = Str::TryTo<NsIndex>( string{nsPath[0]} );
				if( specifiedNs ){
					ns = *specifiedNs;
					path = Str::Join( std::span{nsPath}.subspan(1), "~" );
				}
			}
			elem->includeSubtypes = elem->isInverse = false;
			elem->targetName = { ns, AllocUAString(path) };
		}
	}

namespace Browse{
	Ω onResponse( UA_Client* /*ua*/, void* userdata, RequestId /*requestId*/, UA_BrowseResponse* response )ι->void;
	α FoldersAwait::Suspend()ι->void{
		ASSERT( Promise() );
		_client->PostUA( [this]{//UA submission must run on the client's strand.
			try{
				DBGT( BrowseTag, "[{}]SendBrowseRequest", hex(_client->Handle()) );
				UACε( UA_Client_sendAsyncBrowseRequest(_client->UAPointer(), &_request, onResponse, this, &_requestId) );
				TRACET( BrowseTag, "[{}.{}]SendBrowseRequest", hex(_client->Handle()), hex(_requestId) );
				_client->Process( _requestId, "BrowseRequest" );
			}
			catch( UAException& e ){
				ResumeExp( move(e) );
			}
		});
	}
	α onResponse( UA_Client* /*ua*/, void* userdata, RequestId /*requestId*/, UA_BrowseResponse* response )ι->void {
		FoldersAwait& await = *( FoldersAwait* )userdata;
		await.OnComplete( response );
	}
	α FoldersAwait::OnComplete( UA_BrowseResponse* response )ι->void{
		ASSERT( Promise() );
		_client->ClearRequest( _requestId );
		let sc = response->responseHeader.serviceResult;
		DBGT( BrowseTag, "[{}.{}]({})SendBrowseRequest::Complete", hex(_client->Handle()), hex(_requestId), hex(sc) );
		if( !UA_StatusCode_isBad(sc) ){//a Good_*/Uncertain_* informational code still carries the results.
			if( auto resultSC = !_request.PerNode && response->resultsSize>0 ? response->results[0].statusCode : UA_STATUSCODE_GOOD; UA_StatusCode_isBad(resultSC) ){
				DBGT( BrowseTag, "[{}.{}]({})SendBrowseRequest::Results Error", hex(_client->Handle()), hex(_requestId), hex(resultSC) );
				ResumeExp( UAClientException{resultSC, _client->Handle(), _requestId} );
			}else{
#ifdef __cpp_lib_move_only_function
				Post<Response>( move(*response), move(_h) );
#else
				Post( [r=UA_BrowseResponse{*response},h=_h]()mutable{
					h.promise().Resume(Response{move(r)}, h);
				});
				UA_BrowseResponse_init( response );
#endif
			}
		}else
			ResumeExp( UAClientException{sc, _client->Handle(), _requestId} );
	}
}

	ObjectsFolderAwait::ObjectsFolderAwait( NodeId node, bool snapshot, sp<UAClient> ua, SL sl )ι:
		base{ sl },
		_client{ ua },
		_node{ node },
		_snapshot{ snapshot }
	{}


	α ObjectsFolderAwait::Execute()ι->TAwait<Browse::Response>::Task{
		bool retry{};
		try{
			auto response = co_await Browse::FoldersAwait{ _node, UA_BROWSERESULTMASK_ALL, _client };
			THROW_IF( response.Nodes().size()==0, "No items found for: {}", _node.ToString() );
			if( _snapshot )
				Snapshot( move(response) );
			else
				Attributes( response.Variables(), move(response) );
		}
		catch( UAClientException& e ){
			if( retry=e.IsBadSession(); retry )
				e.PrependWhat( "Retry ObjectsFolder.  " );
			else
				ResumeExp( move(e) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
		if( retry )
			Retry();
	}


	α ObjectsFolderAwait::Snapshot( Browse::Response response )ι->TAwait<flat_map<NodeId, Value>>::Task{
		try{
			if( !_client->Connected ){
				let slug = _client->Slug();
				_client = UAClient::Find( slug, _client->Credential );
				THROW_IF( !_client, "Could not find UAClient for: {}", slug );
			}
			auto vars = response.Variables();
			auto values = vars.size() ? co_await ReadValueAwait{ vars, _client } : flat_map<NodeId, Value>{};
			Attributes( move(vars), move(response), move(values) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α ObjectsFolderAwait::Attributes( flat_set<NodeId>&& variables, Browse::Response response, flat_map<NodeId, Value> values )ι->TAwait<flat_map<NodeId, variant<NodeId, StatusCode>>>::Task{
		try{
			auto dataTypes = variables.size() ? co_await DataTypeAttribAwait{ move(variables), move(_client) } : flat_map<NodeId, variant<NodeId, StatusCode>>{};
			Resume( response.ToJson(move(values), move(dataTypes)) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α ObjectsFolderAwait::Retry()ι->VoidAwait::Task{
		try{
			co_await AwaitSessionActivation( _client );
			[]( ObjectsFolderAwait&& self )->Task {
				try{
					auto j = co_await ObjectsFolderAwait{ self._node, self._snapshot, self._client, self._sl };
					self.Resume( move(j) );
				}
				catch( runtime_error& e ){
					self.ResumeExp( move(e) );
				}
			}( move(*this) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
namespace Browse{
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
		UA_BrowseRequest{ .requestedMaxReferencesPerNode=0, .nodesToBrowseSize=1, .nodesToBrowse=UA_BrowseDescription_new() }{
		nodesToBrowse[0].nodeId = id.Move(); //not move(id): that slices, and ~NodeId then frees the id the in-flight request points at.
	 	nodesToBrowse[0].resultMask = mask;
	}
	Request::Request( vector<NodeId>&& ids, UA_BrowseResultMask mask )ι:
		UA_BrowseRequest{ .requestedMaxReferencesPerNode=0, .nodesToBrowseSize=ids.size(), .nodesToBrowse=(UA_BrowseDescription*)UA_Array_new(ids.size(), &UA_TYPES[UA_TYPES_BROWSEDESCRIPTION]) }{
		for( uint i=0; i<ids.size(); ++i ){
			nodesToBrowse[i].nodeId = ids[i].Move();//Move(), as above:  a slice would leave ~NodeId freeing the id the in-flight request points at.
			nodesToBrowse[i].resultMask = mask;
		}
	}

	α calcMask( const QL::TableQL& ql )ι->UA_BrowseResultMask{
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

	//A steal, like CallResponse/WriteResponse/ReadResponse.  This was a UA_BrowseResponse_copy followed by the _init:
	//x's arrays deep-copied into *this, then x zeroed without a clear - so the originals, the buffers open62541 decoded
	//off the wire, leaked.  IExpectedPromise::SetValue assigns into a variant that still holds the previous co_await's
	//(moved-from) Response, so every browse after the first in one coroutine - NodeIndex::Crawl's second BFS level on -
	//came through here.
	α Response::operator=( Response&& x )ι->Response&{
		if( this!=&x ){
			UA_BrowseResponse_clear( this );
			*(UA_BrowseResponse*)this = x;
			Attribs = x.Attribs;
			UA_BrowseResponse_init( &x );
			x.Attribs = UA_BROWSERESULTMASK_NONE;
		}
		return *this;
	}

	α Response::Nodes()Ι->flat_set<NodeId>{
		flat_set<NodeId> y;
		for( uint i = 0; i < resultsSize; ++i ) {
      for( size_t j = 0; j < results[i].referencesSize; ++j )
				y.emplace( results[i].references[j].nodeId.nodeId );
		}
		return y;
	}

	α Response::Variables()Ι->flat_set<NodeId>{
		flat_set<NodeId> y;
		for( let& result : Iterable<UA_BrowseResult>(results, resultsSize) ){
			for( let& ref : Iterable<UA_ReferenceDescription>(result.references, result.referencesSize) ){
				if( ref.nodeClass == UA_NODECLASS_VARIABLE )
					y.emplace( move(ref.nodeId.nodeId) );
			}
		}
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
				o["browse"] = BrowseName::ToJson( ref.browseName );
			if( Attribs & UA_BROWSERESULTMASK_ISFORWARD )
				o["isForward"] = ref.isForward;
			if( Attribs & UA_BROWSERESULTMASK_DISPLAYNAME )
				o["displayName"] = LocalizedText::ToJson( move(ref.displayName) );
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
	α Response::ToJson( flat_map<NodeId, Value>&& snapshot, flat_map<NodeId, variant<NodeId, StatusCode>>&& dataTypes )ε->jobject{
		jarray references;
		for( size_t i = 0; i < resultsSize; ++i ) {
			for( size_t j = 0; j < results[i].referencesSize; ++j ) {
				UA_ReferenceDescription& ref = results[i].references[j];
				const NodeId nodeId{ move(ref.nodeId.nodeId) };
				jobject reference;
				if( auto p = snapshot.find(nodeId); p!=snapshot.end() )
					reference["value"] = p->second.ToJson();
				if( auto p = dataTypes.find(nodeId); p!=dataTypes.end() ){
					if( std::holds_alternative<StatusCode>(p->second) )
						reference["dataType"] = jobject{ {"sc", std::get<StatusCode>(p->second)} };
					else
						reference["dataType"] = std::get<NodeId>( p->second ).ToJson();
				}
				reference["refType"] = Opc::ToJson( ref.referenceTypeId );
				reference["isForward"] = ref.isForward;
				reference["node"] = nodeId.ToJson();

				jobject bn;
				const UA_QualifiedName& browseName = ref.browseName;
				bn["ns"] = browseName.namespaceIndex;
				bn["name"] = ToSV( browseName.name );
				reference["browse"] = move( bn );

				jobject dn;
				const UA_LocalizedText& displayName = ref.displayName;
				dn["locale"] = ToSV( displayName.locale );
				dn["text"] = ToSV( displayName.text );
				reference["displayName"] = move( dn );

				reference["nodeClass"] = ref.nodeClass;
				reference["typeDef"] = Opc::ToJson( ref.typeDefinition );

				references.push_back( move(reference) );
			}
		}
		jobject j;
		j["refs"] = move( references );
		return j;
	}
}}