#pragma once
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Owned.h>
#include <jde/opc/uatypes/Value.h>
#include "../async/UAAwait.h"

namespace Jde::QL{ struct TableQL; }
namespace Jde::Opc::Gateway{
	struct UAClient;
	//One TranslateBrowsePaths path:  `segments` from `start`, each a browse name in `defaultNS` or its own `ns~name`, over
	//HierarchicalReferences with subtypes - Organizes, HasComponent, HasProperty and the rest, as a browse walks them.
	//A browse name already spelled by the server's namespace index:  a template path's segment once its URI is looked up, for
	//TranslateBrowsePathsAwait (spec *Templates*), which the `ns~name` form can't carry - a URI holds '/' and a browse name may hold '~'.
	struct BrowseSegment final{ NsIndex Ns; string Name; };
	struct UABrowsePath : Owned<UA_BrowsePath, UA_TYPES_BROWSEPATH>{
		UABrowsePath( std::span<const sv> segments, NsIndex defaultNS, const NodeId& start=NodeId::ObjectsFolder() )ι;
		UABrowsePath( std::span<const BrowseSegment> segments, const NodeId& start )ι;
	};
namespace Browse{
	struct Response : Owned<UA_BrowseResponse, UA_TYPES_BROWSERESPONSE>{
		Response()ι{ ASSERT( false ); }
		Response( UA_BrowseResponse&& x )ι:Owned{ move(x) }{}

		α VisitWhile( uint resultsIndex, function<bool(const UA_ReferenceDescription& ref)> f )Ι->bool;
		α SetJson( flat_map<NodeId, jobject>& children, bool addId )Ι->void;

		UA_BrowseResultMask Attribs{ UA_BROWSERESULTMASK_ALL };
	};

	struct Request : Owned<UA_BrowseRequest, UA_TYPES_BROWSEREQUEST>{
		Request( NodeId&& id, UA_BrowseResultMask mask )ι;
		Request( vector<NodeId>&& ids, UA_BrowseResultMask mask )ι;//one BrowseDescription per id;  results[i] answers ids[i].
		Request( NodeId&& id, const QL::TableQL& ql )ι;
		Ω Hierarchical( NodeId&& id, UA_BrowseResultMask mask )ι->Request;//forward HierarchicalReferences (subtypes included), objects/variables/methods only - the NodeIndex crawl.
		Ω Hierarchical( vector<NodeId>&& ids, UA_BrowseResultMask mask )ι->Request;//the same, for a whole BFS level in one round trip.  PerNode.
		Ω Properties( NodeId&& id )ι->Request;//forward HasProperty (no subtypes), variables only, browse names - a DataType's EnumValues/EnumStrings (EnumTypeCache).
		Ω Parents( NodeId&& id )ι->Request;//INVERSE HierarchicalReferences (subtypes included), objects/variables, browse name + class - one step of the walk up to the Objects folder (NodeQLAwait::Path).
		Ω Parents( vector<NodeId>&& ids )ι->Request;//the same, one step for every node still walking in one round trip (HistGroupHost::Describe).  PerNode.
		bool PerNode{};//a bad results[i].statusCode is the caller's to read - one unbrowsable node does not fail the request.  Set by the request, so a caller cannot forget it.
	};

	struct FoldersAwait final : UAAwait<Response>, noncopyable{
		FoldersAwait( NodeId id, UA_BrowseResultMask mask, sp<UAClient>& c, SRCE )ι:UAAwait{ c, sl }, _request{move(id), mask}{}
		FoldersAwait( NodeId id, const QL::TableQL& ql, sp<UAClient>& c, SRCE )ι:UAAwait{ c, sl }, _request{move(id), ql}{}
		FoldersAwait( Request&& request, sp<UAClient>& c, SRCE )ι:UAAwait{ c, sl }, _request{move(request)}{}

		α Suspend()ι->void override;
		α OnComplete( UA_BrowseResponse* response )ι->void;
	private:
		Request _request;
	};
}
}