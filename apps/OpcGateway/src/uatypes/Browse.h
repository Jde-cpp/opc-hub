#pragma once
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Owned.h>
#include <jde/opc/uatypes/Value.h>
#include "../async/UAAwait.h"
#include "ExpectedNodeId.h"

namespace Jde::QL{ struct TableQL; }
namespace Jde::Opc::Gateway{
	struct UAClient;
	//One TranslateBrowsePaths path:  `segments` from `start`, each a browse name in `defaultNS` or its own `ns~name`, over
	//HierarchicalReferences with subtypes - Organizes, HasComponent, HasProperty and the rest, as a browse walks them.  A page path
	//spells a namespace-0 name bare too (NodeQLAwait::segment), which a translate from `defaultNS` cannot follow:  the one place
	//it is known for sure is the first segment from Root, whose children are the standard's Objects, Types and Views, so a bare
	//first segment from Root goes as namespace 0;  elsewhere the path form's level-by-level browse takes namespace 0 where the
	//default namespace has no child of the name.
	//A browse name already spelled by the server's namespace index:  a template path's segment once its URI is looked up, for
	//TranslateBrowsePathsAwait (spec *Templates*), which the `ns~name` form can't carry - a URI holds '/' and a browse name may hold '~'.
	struct BrowseSegment final{ NsIndex Ns; string Name; };
	struct UABrowsePath : Owned<UA_BrowsePath, UA_TYPES_BROWSEPATH>{
		UABrowsePath( std::span<const sv> segments, NsIndex defaultNS, const NodeId& start=NodeId::ObjectsFolder() )ι;
		UABrowsePath( std::span<const BrowseSegment> segments, const NodeId& start )ι;
	};
	//What a `path` - a/b/c, each segment `ns~name` or a bare name in `defaultNs` - asks TranslateBrowsePathsAwait from `start`, the
	//Objects folder unless a caller has a root (a template's, spec *Templates*):  the path, and with `parents` every prefix of it,
	//longest first;  each with its string, the key its answer is filed under by ByPath.
	struct BrowsePathSet final{ vector<string> Paths; vector<UABrowsePath> Requests; };
	α PathPrefixes( sv path, bool parents, NsIndex defaultNs, const NodeId& start=NodeId::ObjectsFolder() )ι->BrowsePathSet;
	α ByPath( const vector<string>& paths, vector<ExpectedNodeId>&& nodes )ι->flat_map<string,ExpectedNodeId>;//the i-th answer under the i-th path.
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
		Ω Hierarchical( NodeId&& id, UA_BrowseResultMask mask )ι->Request;//forward HierarchicalReferences (subtypes included), objects/variables/methods only - the NodeIndex crawl.
		Ω Hierarchical( vector<NodeId>&& ids, UA_BrowseResultMask mask )ι->Request;//the same, for a whole BFS level in one round trip.  PerNode.
		Ω Children( NodeId&& id, const QL::TableQL& ql )ι->Request;//forward HierarchicalReferences (subtypes included), every class, the sub-query's columns - a node page's `children` (NodeQLAwait):  what hangs under the node, never its type or modelling rule.
		Ω Properties( NodeId&& id, UA_BrowseResultMask mask=UA_BROWSERESULTMASK_BROWSENAME )ι->Request;//forward HasProperty (no subtypes), variables only - a DataType's EnumValues/EnumStrings by browse name (EnumTypeCache), a method's arguments (MethodArgumentCache).
		Ω Properties( NodeId&& id, const QL::TableQL& ql )ι->Request;//the same by the sub-query's columns - a Variable page's `properties` (NodeQLAwait).
		Ω References( NodeId&& id )ι->Request;//BOTH directions over References (subtypes included), every class, every result field - a node page's `references` (NodeQLAwait).
		Ω Supertype( NodeId&& id )ι->Request;//INVERSE HasSubtype (no subtypes), data types only - one step of a DataType's walk up to namespace 0 (BaseTypeCache).
		Ω Parents( vector<NodeId>&& ids )ι->Request;//INVERSE HierarchicalReferences (subtypes included), every class, browse name + class - one step of the walk up to the Objects folder or Root for every node still walking, in one round trip (ParentPathsAwait).  PerNode.
		bool PerNode{};//a bad results[i].statusCode is the caller's to read - one unbrowsable node does not fail the request.  Set by the request, so a caller cannot forget it.
	};

	//The reference's browse name is namespace 0's `name`:  a standard property's - InputArguments, EnumValues.
	Ξ IsNs0Name( const UA_ReferenceDescription& ref, sv name )ι->bool{ return ref.browseName.namespaceIndex==0 && ToSV(ref.browseName.name)==name; }
	struct FoldersAwait final : UAAwait<Response>, noncopyable{
		FoldersAwait( NodeId id, UA_BrowseResultMask mask, sp<UAClient>& c, SRCE )ι:UAAwait{ c, sl }, _request{move(id), mask}{}
		FoldersAwait( Request&& request, sp<UAClient>& c, SRCE )ι:UAAwait{ c, sl }, _request{move(request)}{}

		α Suspend()ι->void override;
		α OnComplete( UA_BrowseResponse* response )ι->void;
	private:
		Request _request;
	};
}
}