#include "NodeQLAwait.h"
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/utils/collections.h>
#include <jde/opc/uatypes/BrowseName.h>
#include <jde/opc/uatypes/Variant.h>
#include "../UAClient.h"
#include "../async/ConnectAwait.h"
#include "../async/ParentPathsAwait.h"
#include "../async/ReadValueAwait.h"
#include "../async/TranslateBrowsePathsAwait.h"

#define let const auto
namespace Jde::Opc::Gateway{
	//A browse name as a path segment, spelled as the search index and PathPrefixes spell them:  the bare name in the
	//connection's default namespace, `ns~name` otherwise.  None for a name holding a '/', the url's own separator.
	Ω segment( NsIndex ns, sv name, NsIndex defaultNs )ι->optional<string>{
		if( name.find('/')!=sv::npos )
			return {};
		return ns==defaultNs ? string{ name } : Ƒ( "{}~{}", ns, name );
	}
	Ω segment( const UA_QualifiedName& browse, NsIndex defaultNs )ι->optional<string>{ return segment( browse.namespaceIndex, ToSV(browse.name), defaultNs ); }

	//One frame, every await through Any().  Three forms - a browse path (with its parents), a node's children, or nodes by id
	//(with their browse paths when `path` is asked for).
	α NodeQLAwait::Execute()ι->TAwait<BrowsePathResponse>::Task{
		try{
			jvalue y;
			sv key{ "node" };
			if( auto nodePath = _query.FindPtr<jstring>("path"); nodePath ){
				auto parentsQL = _query.FindTable( "parents" );
				auto prefixes = PathPrefixes( *nodePath, parentsQL!=nullptr, _client->DefaultBrowseNs() );
				auto pathNodes = ByPath( prefixes.Paths, co_await Any(TranslateBrowsePathsAwait{move(prefixes.Requests), _client, _sl}) );
				const bool savePath{ parentsQL && parentsQL->FindColumn("path") };
				flat_map<NodeId, jobject> jParents;
				auto node = pathNodes.begin();
				for( ; node!=pathNodes.end() && node->second.has_value(); ++node ){
					if( savePath )
						jParents.try_emplace( node->second->nodeId, jobject{{"path", node->first}} );
				}
				THROW_IFX( node==pathNodes.begin() && node!=pathNodes.end(), UAClientException(node->second.error(), _client->Handle(), {}, _sl) );
				using Expected = ExpectedNodeId::Expected;
				Expected target = node==pathNodes.begin() ? Expected{ ExNodeId{NodeId{}} } : Expected{ std::prev(node)->second };
				//The translate stopped at `node`:  browse on from its parent a level at a time, by browse name.
				for( auto parent = node==pathNodes.begin() ? node : std::prev(node); target && node!=pathNodes.end(); parent = node, ++node ){
					Browse::FoldersAwait browse{ parent->second->nodeId, UA_BROWSERESULTMASK_BROWSENAME, _client, _sl };
					let response = co_await Any( browse );
					const BrowseName reqBrowse{ Str::Split(node->first,'/').back(), _client->DefaultBrowseNs() };
					let found = !response.VisitWhile( 0, [&](const UA_ReferenceDescription& ref){
						let shouldContinue = reqBrowse!=ref.browseName;
						if( !shouldContinue ){
							target = ExNodeId{ ref.nodeId };
							if( savePath )
								jParents.try_emplace( target->nodeId, jobject{{"path", node->first}} );
							node->second = target;
						}
						return shouldContinue;
					} );
					if( !found )
						target = node->second;
				}
				jobject jReqNode;
				if( target ){
					jParents.erase( target->nodeId );//the deepest node is the answer;  the rest of jParents are its parents.
					ReadRequest request{ {target->nodeId}, _query };
					if( parentsQL )
						request.Add( *parentsQL, jParents );
					ReadAwait read{ move(request), _client };
					auto resp = co_await Any( read );
					auto json = resp.GetJson();
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
			else if( auto children = _query.FindTable("children"); children ){
				QL::TableQL childrenQL{ move(*children) };
				Browse::FoldersAwait browse{ NodeId{_query}, childrenQL, _client, _sl };
				auto browseResp = co_await Any( browse );
				flat_map<NodeId, jobject> jChildren;
				browseResp.SetJson( jChildren, childrenQL.FindColumn("id") );
				ReadAwait read{ move(browseResp), move(childrenQL), _client };
				auto resp = co_await Any( read );
				resp.SetJson( jChildren );
				jarray a;
				for( auto&& [id, j] : jChildren )
					a.emplace_back( move(j) );
				y = jobject{ {"children", move(a)} };
			}
			else{
				key = "nodes";
				let nodeIds = NodeId::ParseQL( _query );
				//`path` on the id form - the reverse of the path form above, for a node known only by id:  the node-scoped resources
				//access holds (`criteria` = "ns=5;i=5005") link to the node's page, which is addressed by browse path.  The walk up to
				//the Objects folder is ParentPathsAwait's, for every node at once - a node outside that tree (types, views, the Root)
				//has no page and answers null, as does one with an unroutable segment.  A node's own name is not in an inverse result
				//(those carry the parents'), so it is one attribute read for them all.
				flat_map<NodeId, jvalue> paths;
				if( _query.FindColumn("path") ){
					const NodeId objects = NodeId::ObjectsFolder();
					let defaultNs = _client->DefaultBrowseNs();
					ReadAwait readOwn{ ReadRequest{nodeIds, UA_ATTRIBUTEID_BROWSENAME}, _client };
					auto own = co_await Any( readOwn );
					vector<NodeId> walked; vector<string> owns;
					for( uint i=0; i<nodeIds.size(); ++i ){
						let& requested = nodeIds[i];
						optional<string> ownSegment;
						if( i<own.resultsSize && own.results[i].status==UA_STATUSCODE_GOOD && UA_Variant_hasScalarType(&own.results[i].value, &UA_TYPES[UA_TYPES_QUALIFIEDNAME]) )
							ownSegment = segment( *(const UA_QualifiedName*)own.results[i].value.data, defaultNs );
						if( ownSegment && UA_NodeId_equal(&requested, &objects) )
							paths.emplace( NodeId{requested}, jvalue{""} );//the folder itself:  its own name is not a segment.
						else if( ownSegment ){
							walked.push_back( requested );
							owns.push_back( move(*ownSegment) );
						}
						else
							paths.emplace( NodeId{requested}, jvalue{nullptr} );
					}
					PathParents parents;
					let found = co_await ParentPathsAwait{ walked, parents, _client };
					for( uint k=0; k<found.size(); ++k ){
						jvalue path{ nullptr };
						if( let& names = found[k].Parents; names ){
							vector<string> segments;
							for( let& parent : *names ){
								if( auto routable = segment(parent.Ns, parent.Name, defaultNs); routable )
									segments.push_back( move(*routable) );
								else
									break;
							}
							if( segments.size()==names->size() ){
								segments.push_back( move(owns[k]) );
								path = Str::Join( segments, "/" );
							}
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
						nodes = resp.GetJson();
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
					y = resp.ToJson( _query );
				}
			}
			Resume( _query.ReturnRaw ? move(y) : jobject{{key, move(y)}} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}