#pragma once
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>

namespace Jde::QL{ struct TableQL; }
namespace Jde::Opc::Gateway{
	struct UAClient;
	struct UABrowsePath : UA_BrowsePath, noncopyable{
		UABrowsePath( std::span<const sv> segments, NsIndex defaultNS )ι;
		UABrowsePath( UABrowsePath&& x )ι:UA_BrowsePath{ x }{ UA_BrowsePath_init( &x ); }
		//UABrowsePath( const UABrowsePath& x )ι{ UA_BrowsePath_copy( &x, this ); }
		~UABrowsePath(){ UA_BrowsePath_clear(this); }
	};
namespace Browse{
	struct Response : UA_BrowseResponse{
		Response()ι{ ASSERT( false ); }
		Response( UA_BrowseResponse&& x )ι:UA_BrowseResponse{ x }{ UA_BrowseResponse_init( &x ); }
		Response( Response&& x )ι:UA_BrowseResponse{ x },Attribs{x.Attribs}{ UA_BrowseResponse_init( &x ); x.Attribs=UA_BROWSERESULTMASK_NONE; }
		~Response(){ UA_BrowseResponse_clear(this); }
		α operator=( Response&& x )ι->Response&;

		α VisitWhile( uint resultsIndex, function<bool(const UA_ReferenceDescription& ref)> f )Ι->bool;
		α Nodes()Ι->flat_set<NodeId>;
		α Variables()Ι->flat_set<NodeId>;
		α SetJson( flat_map<NodeId, jobject>& children, bool addId )Ι->void;
		α ToJson( flat_map<NodeId, Value>&& snapshot, flat_map<NodeId, variant<NodeId, StatusCode>>&& dataTypes )ε->jobject;

		UA_BrowseResultMask Attribs{ UA_BROWSERESULTMASK_ALL };
	};

	struct Request :UA_BrowseRequest{
		Request( NodeId&& id, UA_BrowseResultMask mask )ι;
		Request( vector<NodeId>&& ids, UA_BrowseResultMask mask )ι;//one BrowseDescription per id;  results[i] answers ids[i].
		Request( NodeId&& id, const QL::TableQL& ql )ι;
		Ω Hierarchical( NodeId&& id, UA_BrowseResultMask mask )ι->Request;//forward HierarchicalReferences (subtypes included), objects/variables/methods only - the NodeIndex crawl.
		Ω Hierarchical( vector<NodeId>&& ids, UA_BrowseResultMask mask )ι->Request;//the same, for a whole BFS level in one round trip.  PerNode.
		Ω Properties( NodeId&& id )ι->Request;//forward HasProperty (no subtypes), variables only, browse names - a DataType's EnumValues/EnumStrings (EnumTypeCache).
		Ω Parents( NodeId&& id )ι->Request;//INVERSE HierarchicalReferences (subtypes included), objects/variables, browse name + class - one step of the walk up to the Objects folder (NodeQLAwait::Path).
		Request( Request&& x )ι:UA_BrowseRequest{ x }, PerNode{ x.PerNode }{ UA_BrowseRequest_init( &x );}
		Request( const Request& x )ι:PerNode{ x.PerNode }{ UA_BrowseRequest_copy( &x, this ); }
		~Request(){ UA_BrowseRequest_clear(this); }
		bool PerNode{};//a bad results[i].statusCode is the caller's to read - one unbrowsable node does not fail the request.  Set by the request, so a caller cannot forget it.
	};

	struct FoldersAwait final : TAwait<Response>, noncopyable{
		FoldersAwait( NodeId id, UA_BrowseResultMask mask, sp<UAClient>& c, SRCE )ι:TAwait<Response>{sl},_client{c}, _request{move(id), mask}{}
		FoldersAwait( NodeId id, const QL::TableQL& ql, sp<UAClient>& c, SRCE )ι:TAwait<Response>{sl},_client{c}, _request{move(id), ql}{}
		FoldersAwait( Request&& request, sp<UAClient>& c, SRCE )ι:TAwait<Response>{sl},_client{c}, _request{move(request)}{}

		α Suspend()ι->void override;
		α OnComplete( UA_BrowseResponse* response )ι->void;
	private:
		sp<UAClient> _client;
		Request _request;
		RequestId _requestId{};
	};
}
	struct ObjectsFolderAwait final : TAwaitEx<jobject, TAwait<Browse::Response>::Task>{
		using base = TAwaitEx<jobject,TAwait<Browse::Response>::Task>;
		ObjectsFolderAwait( NodeId node, bool snapshot, sp<UAClient> ua, SRCE )ι;
	private:
		α Execute()ι->TAwait<Browse::Response>::Task;
		α Snapshot( Browse::Response response )ι->TAwait<flat_map<NodeId, Value>>::Task;
		α Attributes( flat_set<NodeId>&& variables, Browse::Response response, flat_map<NodeId, Value> values={} )ι->TAwait<flat_map<NodeId, variant<NodeId, StatusCode>>>::Task;
		α Retry()ι->VoidAwait::Task;
		sp<UAClient> _client; NodeId _node; bool _snapshot;
	};
}