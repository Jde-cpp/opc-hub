#include "NodeQLAwait.h"
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/utils/collections.h>
#include <jde/opc/uatypes/BrowseName.h>
#include <jde/opc/uatypes/Variant.h>
#include "../UAClient.h"
#include "../async/ConnectAwait.h"
#include "../async/ParentPathsAwait.h"
#include "../async/ReadValueAwait.h"
#include "../async/ReleaseBrowseAwait.h"
#include "../async/TranslateBrowsePathsAwait.h"

#define let const auto
namespace Jde::Opc::Gateway{
	//A browse name as a path segment, spelled as the search index and PathPrefixes spell them:  the bare name in the
	//connection's default namespace or in namespace 0 - the standard's Objects, Types, Views and everything under Types - `ns~name`
	//otherwise.  A namespace-0 `twin`, beside a default-namespace sibling of the same name, is `0~name`:  childNamed takes the bare
	//name for the default's (reviews-issue/416-address-space.md #7).  None for a name holding a '/', the url's own separator.
	Ω segment( NsIndex ns, sv name, NsIndex defaultNs, bool twin=false )ι->optional<string>{
		if( name.find('/')!=sv::npos )
			return {};
		return ns==defaultNs || (ns==0 && !twin) ? string{ name } : Ƒ( "{}~{}", ns, name );
	}
	Ω segment( const UA_QualifiedName& browse, NsIndex defaultNs )ι->optional<string>{ return segment( browse.namespaceIndex, ToSV(browse.name), defaultNs ); }

	//The path form's start:  `root`, Objects by default - today's urls and the historian's template paths - or Root, the web's pages.
	Ω rootOf( const QL::TableQL& q )ε->NodeId{
		let root = q.FindPtr<jstring>( "root" );
		if( !root || *root=="Objects" )
			return NodeId::ObjectsFolder();
		THROW_IF( *root!="Root", "root is 'Objects' or 'Root', not '{}'.", *root );
		return NodeId::RootFolder();
	}
	//The child of a browse named by a segment:  `ns~name` exactly;  a bare name in the default namespace, else in namespace 0
	//(Browse.h) - the default's first, whatever the browse's order.
	Ω childNamed( const Browse::Response& response, sv segment, NsIndex defaultNs )ε->optional<NodeId>{
		const BrowseName asked{ segment, defaultNs };
		optional<NodeId> y, ns0;
		response.VisitWhile( 0, [&]( const UA_ReferenceDescription& ref ){
			if( asked==ref.browseName )
				y = NodeId{ ref.nodeId.nodeId };
			else if( !ns0 && asked.namespaceIndex!=0 && segment.find('~')==sv::npos && ref.browseName.namespaceIndex==0 && ToSV(ref.browseName.name)==segment )
				ns0 = NodeId{ ref.nodeId.nodeId };
			return !y;
		} );
		return y ? y : ns0;
	}
	//A node id with its DisplayName, where the cache has one - a `references` row's reference type and type definition.
	Ω named( const UA_NodeId& id, const DisplayNames& names )ι->jvalue{
		if( UA_NodeId_isNull(&id) )
			return nullptr;
		jobject y = Opc::ToJson( id );
		if( auto p = names.find(id); p!=names.end() )
			y["name"] = p->second;
		return y;
	}

	//One frame, every await through Any().  Four forms - a browse path (with its parents), a node's children or properties, a
	//node's references, or nodes by id (with their browse paths when `path` is asked for).
	α NodeQLAwait::Execute()ι->TAwait<BrowsePathResponse>::Task{
		try{
			jvalue y;
			sv key{ "node" };
			let defaultNs = _client->DefaultBrowseNs();
			if( auto nodePath = _query.FindPtr<jstring>("path"); nodePath ){
				auto parentsQL = _query.FindTable( "parents" );
				const NodeId start = rootOf( _query );
				auto prefixes = PathPrefixes( *nodePath, parentsQL!=nullptr, defaultNs, start );
				auto pathNodes = ByPath( prefixes.Paths, co_await Any(TranslateBrowsePathsAwait{move(prefixes.Requests), _client, _sl}) );
				const bool savePath{ parentsQL && parentsQL->FindColumn("path") };
				flat_map<NodeId, jobject> jParents;
				auto node = pathNodes.begin();//the prefixes sort shortest first, so this stops at the first the translate refused.
				for( ; node!=pathNodes.end() && node->second.has_value(); ++node ){
					if( savePath )
						jParents.try_emplace( node->second->nodeId, jobject{{"path", node->first}} );
				}
				THROW_IFX( node==pathNodes.begin() && node!=pathNodes.end() && Str::Split(node->first,'/').empty(), UAClientException(node->second.error(), _client->Handle(), {}, _sl) );//no segments:  the server's own refusal.
				using Expected = ExpectedNodeId::Expected;
				Expected target = node==pathNodes.begin() ? Expected{ ExNodeId{NodeId{start}} } : Expected{ std::prev(node)->second };
				//The translate stopped at `node`:  browse on from the deepest prefix that resolved, or the start, a segment at a time, by
				//browse name - a bare one in the default namespace, else in namespace 0, which the translate could not try.  Without
				//parents{} the one entry is the whole path, so every segment past those resolved is walked.  Over what a page's `children`
				//lists, as the translate goes:  never a type or a modelling rule (reviews-issue/416-address-space.md #11).
				uint resolved = node==pathNodes.begin() ? 0 : Str::Split( std::prev(node)->first, '/' ).size();
				for( ; target && node!=pathNodes.end(); ++node ){
					let segments = Str::Split( node->first, '/' );
					for( ; target && resolved<segments.size(); ++resolved ){
						Browse::FoldersAwait browse{ Browse::Request::Children(NodeId{target->nodeId}, UA_BROWSERESULTMASK_BROWSENAME), _client, _sl };
						let response = co_await Any( browse );
						if( auto found = childNamed(response, segments[resolved], defaultNs); found )
							target = ExNodeId{ *found };
						else
							target = node->second;//the translate's refusal of this prefix
					}
					if( target ){
						if( savePath )
							jParents.try_emplace( target->nodeId, jobject{{"path", node->first}} );
						node->second = target;
					}
				}
				jobject jReqNode;
				if( target ){
					jParents.erase( target->nodeId );//the deepest node is the answer;  the rest of jParents are its parents.
					ReadRequest request{ {target->nodeId}, _query };
					if( parentsQL )
						request.Add( *parentsQL, jParents );
					ReadAwait read{ move(request), _client };
					auto resp = co_await Any( read );
					auto json = resp.GetJson( _client->Types().get() );
					if( auto p = json.find(target->nodeId); p!=json.end() )
						jReqNode = move( p->second );
					for( auto&& [id, jParent] : jParents ){
						if( auto p = json.find(id); p!=json.end() )
							jParent.insert( p->second.begin(), p->second.end() );
					}
					if( _query.FindColumn("id") )
						target->Add( jReqNode );
					TRACET( ELogTags::Test, "jReqNode: {}", serialize(jReqNode) );
				}
				else
					jReqNode = UAException::ToJson( target.error() );
				if( parentsQL ){
					jarray parents;
					for( auto&& [id, jParent] : jParents ){
						if( parentsQL->FindColumn("id") )
							id.Add( jParent );
						parents.emplace_back( move(jParent) );
					}
					jReqNode["parents"] = move( parents );
				}
				y = move( jReqNode );
			}
			else if( auto references = _query.FindTable("references"); references ){
				//Every reference of the node in both directions (Browse::Request::References):  its direction, its type and the target's
				//class and type definition as the server answered them, the type and the type definition named from the cache - one read
				//for those not yet seen.  At most /gateway/browse/maxReferences, `truncated` when there were more - past the cap or the
				//server's own - whose continuation point is released, not followed (reviews-issue/416-address-space.md #9).
				const QL::TableQL referencesQL{ move(*references) };
				const bool direction = referencesQL.FindColumn("direction"), refType = referencesQL.FindColumn("refType"), target = referencesQL.FindColumn("target"), nodeClass = referencesQL.FindColumn("class"), typeDef = referencesQL.FindColumn("typeDef");
				let maxReferences = (UA_UInt32)Settings::FindNumber<uint32>( "/gateway/browse/maxReferences" ).value_or( 1000 );//config/Opc.Gateway.jsonnet
				Browse::FoldersAwait browse{ Browse::Request::References(NodeId{_query}, maxReferences), _client, _sl };
				auto response = co_await Any( browse );
				const bool truncated = response.resultsSize && response.results[0].continuationPoint.length;
				if( truncated ){
					try{
						if( let sc = co_await Any( ReleaseBrowseAwait{response, _client, _sl} ); sc )
							DBGT( BrowseTag, "[{}]references:  the continuation point's release answered {}.", hex(_client->Handle()), UAException::Message(sc) );
					}
					catch( const std::exception& e ){
						DBGT( BrowseTag, "[{}]references:  could not release the continuation point:  {}", hex(_client->Handle()), e.what() );
					}
				}
				flat_set<NodeId> toName;
				response.VisitWhile( 0, [&]( const UA_ReferenceDescription& ref ){
					if( refType )
						toName.emplace( ref.referenceTypeId );
					if( typeDef && !UA_NodeId_isNull(&ref.typeDefinition.nodeId) )
						toName.emplace( ref.typeDefinition.nodeId );
					return true;
				} );
				DisplayNames names;
				if( toName.size() )
					names = co_await _client->DisplayNames().Get( _client, vector<NodeId>{toName.begin(), toName.end()}, _sl );
				jarray rows;
				response.VisitWhile( 0, [&]( const UA_ReferenceDescription& ref ){
					jobject row;
					if( direction )
						row["direction"] = ref.isForward ? "Forward" : "Inverse";
					if( refType )
						row["refType"] = named( ref.referenceTypeId, names );
					if( target ){
						jobject t = Opc::ToJson( ref.nodeId.nodeId );
						t["name"] = Opc::ToString( ref.displayName.text );
						t["browse"] = Opc::ToJson( ref.browseName );
						row["target"] = move( t );
					}
					if( nodeClass )
						row["class"] = ref.nodeClass;
					if( typeDef )
						row["typeDef"] = named( ref.typeDefinition.nodeId, names );
					rows.push_back( move(row) );
					return true;
				} );
				jobject result{ {"references", move(rows)} };
				if( _query.FindColumn("truncated") )
					result["truncated"] = truncated;
				y = move( result );
			}
			else if( auto children = _query.FindTable("children"), properties = _query.FindTable("properties"); children || properties ){
				//What hangs under the node - `children`, its hierarchical references forward, every class, or `properties`, its
				//HasProperty ones, a Variable page's - each row read for the sub-query's attributes in one Read.
				let subKey = children ? "children"sv : "properties"sv;
				QL::TableQL subQL{ move(children ? *children : *properties) };
				Browse::FoldersAwait browse{ children ? Browse::Request::Children(NodeId{_query}, subQL) : Browse::Request::Properties(NodeId{_query}, subQL), _client, _sl };
				auto browseResp = co_await Any( browse );
				flat_map<NodeId, jobject> jChildren;
				browseResp.SetJson( jChildren, subQL.FindColumn("id") );
				ReadAwait read{ move(browseResp), move(subQL), _client };
				auto resp = co_await Any( read );
				resp.SetJson( jChildren, _client->Types().get() );
				jarray a;
				for( auto&& [id, j] : jChildren )
					a.emplace_back( move(j) );
				y = jobject{ {subKey, move(a)} };
			}
			else{
				key = "nodes";
				let nodeIds = NodeId::ParseQL( _query );
				//`path` on the id form - the reverse of the path form above, for a node known only by id:  the node-scoped resources
				//access holds (`criteria` = "ns=5;i=5005") link to the node's page, which is addressed by browse path from Root, as the
				//search's hits are.  The walk up to Root is ParentPathsAwait's, for every node at once - Root itself is the connection's
				//page, with no segment of its own, and a node with an unroutable segment on the way answers null.  A node's own name
				//is not in an inverse result (those carry the parents'), so it is one attribute read for them all.
				flat_map<NodeId, jvalue> paths;
				if( _query.FindColumn("path") ){
					const NodeId root = NodeId::RootFolder();
					ReadAwait readOwn{ ReadRequest{nodeIds, UA_ATTRIBUTEID_BROWSENAME}, _client };
					auto own = co_await Any( readOwn );
					vector<NodeId> walked; vector<BrowseSegment> owns;
					for( uint i=0; i<nodeIds.size(); ++i ){
						let& requested = nodeIds[i];
						optional<BrowseSegment> ownName;
						if( i<own.resultsSize && own.results[i].status==UA_STATUSCODE_GOOD && UA_Variant_hasScalarType(&own.results[i].value, &UA_TYPES[UA_TYPES_QUALIFIEDNAME]) ){
							let& q = *(const UA_QualifiedName*)own.results[i].value.data;
							if( segment(q, defaultNs) )
								ownName = BrowseSegment{ q.namespaceIndex, string{ToSV(q.name)} };
						}
						if( ownName && UA_NodeId_equal(&requested, &root) )
							paths.emplace( NodeId{requested}, jvalue{""} );
						else if( ownName ){
							walked.push_back( requested );
							owns.push_back( move(*ownName) );
						}
						else
							paths.emplace( NodeId{requested}, jvalue{nullptr} );
					}
					PathParents parents;
					let found = co_await ParentPathsAwait{ walked, parents, _client, std::numeric_limits<uint>::max(), root };
					//Each segment of a walked node's path - its parents' names, then its own - with the node it hangs under:  Root for the first.
					auto visit = [&]( uint k, function<void(const BrowseSegment&, const NodeId* under)> f ){
						let& names = *found[k].Parents;
						for( uint j=0; j<=names.size(); ++j )
							f( j<names.size() ? names[j] : owns[k], j ? &found[k].Ids[j-1] : nullptr );
					};
					//The namespace-0 names with a default-namespace sibling of the same name, by the node they hang under, in one translate:
					//those are `0~name` (segment).  Not under Root, whose first segment the path form takes as namespace 0's (Browse.h).
					flat_set<std::pair<NodeId,string>> twins;
					if( defaultNs!=0 ){
						flat_set<std::pair<NodeId,string>> asked;
						for( uint k=0; k<found.size(); ++k ){
							if( found[k].Parents )
								visit( k, [&]( const BrowseSegment& name, const NodeId* under ){ if( under && name.Ns==0 ) asked.emplace( *under, name.Name ); } );
						}
						if( asked.size() ){
							vector<UABrowsePath> requests; requests.reserve( asked.size() );
							for( let& [under, name] : asked ){
								const BrowseSegment twin{ defaultNs, name };
								requests.emplace_back( std::span<const BrowseSegment>{&twin, 1}, under );
							}
							try{
								let answers = co_await Any( TranslateBrowsePathsAwait{move(requests), _client, _sl} );
								uint i{};
								for( let& key : asked ){
									if( i<answers.size() && answers[i].has_value() )
										twins.emplace( key );
									++i;
								}
							}
							catch( const std::exception& e ){
								WARNT( BrowseTag, "[{}]path: could not look for namespace-0 twins - their names stay bare:  {}", hex(_client->Handle()), e.what() );
							}
						}
					}
					for( uint k=0; k<found.size(); ++k ){
						jvalue path{ nullptr };
						if( found[k].Parents ){
							vector<string> segments; bool routable{ true };
							visit( k, [&]( const BrowseSegment& name, const NodeId* under ){
								let twin = under && name.Ns==0 && twins.find( std::pair{*under, name.Name} )!=twins.end();
								if( auto s = routable ? segment(name.Ns, name.Name, defaultNs, twin) : optional<string>{}; s )
									segments.push_back( move(*s) );
								else
									routable = false;
							} );
							if( routable )
								path = Str::Join( segments, "/" );
						}
						else if( found[k].Deep )
							WARNT( BrowseTag, "[{}]path: '{}' is deeper than {} or cyclic - no path.", hex(_client->Handle()), walked[k].ToString(), ParentPathsAwait::MaxDepth );
						paths.emplace( move(walked[k]), move(path) );
					}
				}
				ReadRequest request{ nodeIds, _query };
				if( paths.size() ){//one row per requested id even when `path` is the only column (nothing to read, and a Read of nothing is an error).
					flat_map<NodeId, jobject> nodes;
					if( request.nodesToReadSize ){
						ReadAwait read{ move(request), _client };
						auto resp = co_await Any( read );
						nodes = resp.GetJson( _client->Types().get() );
					}
					jarray rows;
					for( let& id : nodeIds ){
						auto p = nodes.find( id );
						jobject row = p==nodes.end() ? jobject{} : move( p->second );
						if( _query.FindColumn("id") )
							row["id"] = id.ToJson();
						auto path = paths.find( id );
						row["path"] = path==paths.end() ? jvalue{nullptr} : path->second;
						rows.push_back( move(row) );
					}
					y = _query.IsPlural() ? jvalue{ move(rows) } : ( rows.size() ? jvalue{move(rows[0])} : jobject{} );
				}
				else{
					ReadAwait read{ move(request), _client };
					auto resp = co_await Any( read );
					y = resp.ToJson( _query, _client->Types().get() );
				}
			}
			Resume( _query.ReturnRaw ? move(y) : jobject{{key, move(y)}} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}