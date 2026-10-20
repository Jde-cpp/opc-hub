#include "NodeQLAwait.h"
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/utils/collections.h>
#include <jde/opc/uatypes/BrowseName.h>
#include <jde/opc/uatypes/Variant.h>
#include "../UAClient.h"
#include "../async/ConnectAwait.h"
#include "../async/ReadValueAwait.h"
#include "../async/UAStrandAwait.h"

#define let const auto
namespace Jde::Opc::Gateway{
	//A browse name as a path segment, spelled as the search index and BrowsePathsToNodeIds spell them:  the bare name in the
	//connection's default namespace, `ns~name` otherwise.  None for a name holding a '/', the url's own separator.
	Ω segment( const UA_QualifiedName& browse, NsIndex defaultNs )ι->optional<string>{
		string name{ ToSV(browse.name) };
		if( name.find('/')!=string::npos )
			return {};
		return browse.namespaceIndex==defaultNs ? name : Ƒ( "{}~{}", browse.namespaceIndex, name );
	}

	//One frame:  its own type is the UAStrandAwait's, and every other await goes through Any().  Three forms - a browse path
	//(with its parents), a node's children, or nodes by id (with their browse paths when `path` is asked for).
	α NodeQLAwait::Execute()ι->TAwait<BrowsePathResponse>::Task{
		try{
			jvalue y;
			sv key{ "node" };
			if( auto nodePath = _query.FindPtr<jstring>("path"); nodePath ){
				auto parentsQL = _query.FindTable( "parents" );
				auto pathNodes = co_await UAStrandAwait<BrowsePathResponse>{ _client, [this, nodePath, parentsQL]{ return _client->BrowsePathsToNodeIds(*nodePath, parentsQL!=nullptr); }, _sl };//sync UA service - must run on the client's strand.
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
				//BrowsePathsToNodeIds stopped at `node`:  browse on from its parent a level at a time, by browse name.
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
				//access holds (`criteria` = "ns=5;i=5005") link to the node's page, which is addressed by browse path.  Walk up:
				//each inverse HierarchicalReferences browse names the parent, until the Objects folder - a node outside that tree
				//(types, views, the Root) has no page and answers null, as does one with an unroutable segment.  A node's own name
				//is not in an inverse result (those carry the parents'), so it is one attribute read.  Several parents (an object
				//Organized by a folder and a Component of another):  the Objects folder wins when it is one of them, else the
				//first - the tree the crawl reaches the node by.
				flat_map<NodeId, jvalue> paths;
				if( _query.FindColumn("path") ){
					const NodeId objects = NodeId::ObjectsFolder();
					let defaultNs = _client->DefaultBrowseNs();
					for( let& requested : nodeIds ){
						jvalue path{ nullptr };
						vector<string> segments;
						ReadAwait readOwn{ ReadRequest{requested, UA_ATTRIBUTEID_BROWSENAME}, _client };
						auto own = co_await Any( readOwn );
						optional<string> ownSegment;
						if( own.resultsSize && own.results[0].status==UA_STATUSCODE_GOOD && UA_Variant_hasScalarType(&own.results[0].value, &UA_TYPES[UA_TYPES_QUALIFIEDNAME]) )
							ownSegment = segment( *(const UA_QualifiedName*)own.results[0].value.data, defaultNs );
						if( ownSegment )
							segments.push_back( *ownSegment );
						NodeId current{ requested };
						for( uint depth=0; ownSegment; ++depth ){
							if( UA_NodeId_equal(&current, &objects) ){
								segments.pop_back();//the walk started at Objects itself:  its own name is not a segment.
								path = Str::Join( segments, "/" );
								break;
							}
							if( depth>=64 ){//a reference cycle - the walk would never reach Objects.
								WARNT( BrowseTag, "[{}]path: '{}' is deeper than 64 or cyclic - no path.", hex(_client->Handle()), requested.ToString() );
								break;
							}
							Browse::FoldersAwait browse{ Browse::Request::Parents(NodeId{current}), _client, _sl };
							auto response = co_await Any( browse );
							optional<NodeId> parent; optional<string> parentSegment;
							response.VisitWhile( 0, [&]( const UA_ReferenceDescription& ref ){
								NodeId id{ ref.nodeId.nodeId };
								let isObjects = UA_NodeId_equal( &id, &objects );
								if( !parent || isObjects ){
									parent = id;
									parentSegment = segment( ref.browseName, defaultNs );
								}
								return !isObjects;
							} );
							if( !parent )
								break;//no hierarchical parent, and not Objects - outside the tree the pages address.
							if( UA_NodeId_equal(&*parent, &objects) ){
								path = Str::Join( segments, "/" );
								break;
							}
							if( !parentSegment )
								break;//an unroutable parent name.
							segments.insert( segments.begin(), *parentSegment );
							current = *parent;
						}
						paths.emplace( NodeId{requested}, move(path) );
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