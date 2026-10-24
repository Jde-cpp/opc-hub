#include "ParentPathsAwait.h"
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/Logger.h>
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	α ParentPathsAwait::Walk()ι->VoidTask{
		try{
			vector<ParentPath> y( _nodes.size() );
			struct Walker final{ uint Slot; NodeId Current; vector<BrowseSegment> Segments; vector<NodeId> Ids; };//Segments and Ids:  the parents', nearest first.
			vector<Walker> walking;
			for( uint i=0; i<_nodes.size(); ++i )
				walking.push_back( {i, _nodes[i], {}, {}} );
			const NodeId objects = NodeId::ObjectsFolder();
			while( walking.size() ){
				vector<Walker> next; flat_set<NodeId> unknown;
				for( auto& w : walking ){
					for( auto p = _parents.find(w.Current); ; p = _parents.find(w.Current) ){//up through the parents known.
						if( p==_parents.end() ){
							unknown.emplace( w.Current );
							next.push_back( move(w) );
						}
						else if( let& parent = p->second; parent.Up && w.Segments.size()<MaxDepth ){
							w.Segments.push_back( parent.Up->second );
							w.Ids.push_back( parent.Up->first );
							w.Current = parent.Up->first;
							continue;
						}
						else if( parent.Refused )
							y[w.Slot].Lost = true;
						else if( parent.AtObjects ){
							std::ranges::reverse( w.Segments );
							std::ranges::reverse( w.Ids );
							y[w.Slot].Parents = move( w.Segments );
							y[w.Slot].Ids = move( w.Ids );
						}
						else
							y[w.Slot].Deep = parent.Up.has_value();
						break;
					}
				}
				vector<NodeId> asking{ unknown.begin(), unknown.end() };
				for( uint start=0; start<asking.size(); ){
					const vector<NodeId> asked{ asking.begin()+start, asking.begin()+std::min<uint>(start+_batch, asking.size()) };
					optional<Browse::Response> response;
					try{
						Browse::FoldersAwait browse{ Browse::Request::Parents(vector<NodeId>{asked}), _client };
						response = co_await Any( browse );
					}
					catch( const UAException& e ){
						if( e.Code()!=UA_STATUSCODE_BADTOOMANYOPERATIONS || asked.size()==1 )
							throw;
					}
					if( !response ){//the server's MaxNodesPerBrowse is below the request:  fewer, and again.
						_batch = std::max<uint>( 1, asked.size()/2 );
						DBGT( BrowseTag, "[{}]A browse of {} parents was refused as too many - {} at a time.", hex(_client->Handle()), asked.size(), _batch );
						continue;
					}
					for( uint k=0; k<asked.size(); ++k ){
						PathParent parent{ .Refused = k>=response->resultsSize || UA_StatusCode_isBad(response->results[k].statusCode) };
						if( !parent.Refused ){
							optional<NodeId> id; optional<BrowseSegment> name;
							response->VisitWhile( k, [&]( const UA_ReferenceDescription& ref ){
								NodeId candidate{ ref.nodeId.nodeId };
								let isObjects = UA_NodeId_equal( &candidate, &objects );
								if( !id || isObjects ){
									id = move( candidate );
									name = BrowseSegment{ ref.browseName.namespaceIndex, string{ToSV(ref.browseName.name)} };
									parent.AtObjects = isObjects;
								}
								return !isObjects;
							} );
							if( !parent.AtObjects && id )
								parent.Up.emplace( move(*id), move(*name) );
						}
						_parents.emplace( asked[k], move(parent) );
					}
					start += asked.size();
				}
				walking = move( next );
			}
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}