#include "HistHost.h"
#include <jde/fwk/settings.h>
#include <jde/access/Authorize.h>
#include <jde/db/IDataSource.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/LocalQL.h>
#include <jde/opc/UAException.h>
#include "../async/ConnectAwait.h"
#include "../async/NamespacesAwait.h"
#include "../async/TranslateBrowsePathsAwait.h"
#include "../GatewayAppClient.h"
#include "../ql/GatewayQL.h"
#include "../types/ServerCnnctn.h"
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };
	using Opc::Hist::Thresholds;

	α HistResolveAwait::Suspend()ι->void{ HistHost::Instance().Resolving( *this ); }

	α HistHost::Instance()ι->HistHost&{
		static HistHost instance;
		return instance;
	}
	α HistHost::Start()ε->void{
		let hist = Settings::FindObject( "/gateway/hist" );
		if( !hist ){
			INFO( "No '/gateway/hist':  this gateway keeps no history of its own." );
			return;
		}
		auto historian = ms<Opc::Hist::Historian>( Opc::Hist::Settings{*hist, Process::AppDataFolder()/"logs"/"hist"/"opc-gateway"}, Opc::Hist::SystemClock() );
		{
			lg _{ _mutex };
			_historian = historian;
		}
		if( !historian->Enabled() )//the library said why, as Critical.
			return;
		struct LoadAwait final : AnyVoidAwait{
			LoadAwait( HistHost& host )ι:_host{ host }{}
			α Suspend()ι->void override{ _host.Loading( *this ); }
			HistHost& _host;
		};
		BlockAny( LoadAwait{*this} );
	}
	α HistHost::Stop()ι->void{
		vector<sp<HistGroupHost>> hosts;
		sp<Opc::Hist::Historian> historian;
		{
			lg _{ _mutex };
			_stopped = true;
			for( auto&& [_,host] : _groups )
				hosts.push_back( host );
			_groups.clear();
			historian = move( _historian );
		}
		for( auto& host : hosts )
			host->Close();
	}
	α HistHost::TestEnabled( SL sl )Ε->void{
		sp<Opc::Hist::Historian> historian; bool stopped;
		{
			lg _{ _mutex };
			historian = _historian;
			stopped = _stopped;
		}
		if( historian && historian->Enabled() )
			return;
		const ExceptionArgs args{ EHttpStatus::ServiceUnavailable };//at Debug:  the configuration's, which the start logged, not a fault.
		if( stopped )
			throw Exception{ sl, args, "The gateway's historian has stopped." };
		if( !historian )
			throw Exception{ sl, args, "This gateway keeps no history of its own:  it has no /gateway/hist block." };
		throw Exception{ sl, args, "This gateway runs without its historian:  another process holds the lock on '{}', or it can't be made.", historian->Config().Path.string() };
	}
	α HistHost::Find( HistGroupPK id )Ι->sp<HistGroupHost>{
		lg _{ _mutex };
		auto p = _groups.find( id );
		return p==_groups.end() ? nullptr : p->second;
	}
	α HistHost::Groups()Ι->vector<sp<HistGroupHost>>{
		lg _{ _mutex };
		auto y = Reserve<sp<HistGroupHost>>( _groups.size() );
		for( let& [_,host] : _groups )
			y.push_back( host );
		return y;
	}
	α HistHost::Gate( HistGroupPK id, Access::ERights rights, bool enforced, UserPK by, SL sl )ε->sp<HistGroupHost>{
		auto host = Find( id );
		let criteria = host ? host->Guid() : string{};//none names the table's row, as Test falls back to it for a group with no row of its own.
		let acl = AppClient()->Acl();
		if( enforced )
			acl->TestEnforced( "gateway", "histGroups", criteria, rights, by, sl );
		else
			acl->Test( "gateway", "histGroups", criteria, rights, by, sl );
		TestEnabled( sl );
		if( !host )
			throw Exception{ sl, ExceptionArgs{EHttpStatus::NotFound}, "Group {} isn't one this gateway historizes.", id };
		return host;
	}
	α HistHost::Collector( const ServerCnnctnNK& slug )Ι->Credential{
		Credential cred{ Crypto::PublicKey{} };
		if( let credentials = Settings::FindObject(Ƒ("/gateway/hist/credentials/{}", slug)); credentials ){
			if( let user = Json::FindSV(*credentials, "user"); user )
				cred = Credential{ User{string{*user}, string{Json::FindDefaultSV(*credentials, "password")}} };
		}
		return Credential::Collector( move(cred) );
	}
	α HistHost::Writer( UserPK by )Ι->Opc::Hist::Writer{
		let acl = AppClient()->Acl();
		return { by, acl ? acl->UserName( by ) : std::to_string( by.Value ) };
	}

	α SlugAwait::Execute()ι->VoidTask{
		try{
			auto connections = co_await Any( ServerCnnctnAwait{DB::Key{(uint)_connection}, true} );
			if( connections.empty() )
				throw Exception{ _sl, ExceptionArgs{EHttpStatus::NotFound}, "Connection {} not found.", _connection };
			Resume( move(connections.front().Slug) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	Ω memberOf( const flat_map<HistTemplateNodePK,HistTemplateNodeRow>& members, optional<HistTemplateNodePK> id )ι->const HistThresholdColumns*{
		let row = FindHistTemplateNode( members, id );
		return row ? &row->Thresholds : nullptr;
	}

	α HistHost::Load( const HistGroupRow& row, const vector<HistGroupNodeRow>& nodes, const flat_map<HistTemplateNodePK,HistTemplateNodeRow>& members, str slug, SL sl )ε->sp<HistGroupHost>{
		sp<Opc::Hist::Historian> historian;
		{
			lg _{ _mutex };
			THROW_IFSL( _stopped || !_historian, "The historian has stopped." );
			historian = _historian;
		}
		auto initial = Reserve<Opc::Hist::Member>( nodes.size() );
		auto hostMembers = Reserve<HistMember>( nodes.size() );
		for( let& node : nodes ){
			let thresholds = ResolveThresholds( node.Thresholds, memberOf(members, node.TemplateNode), row.Thresholds );
			initial.push_back( {node.Node, HistGroupHost::ForGroup(thresholds), node.Id} );
			hostMembers.push_back( {node.Id, node.Node, thresholds} );
		}
		auto group = historian->AddGroup( {.Name=row.Guid, .Indexes=Opc::Hist::EIndexes::Host, .PublishingInterval=HistGroupHost::PublishingInterval}, move(initial), sl );
		auto host = ms<HistGroupHost>( row, slug, move(group), move(hostMembers), Collector(slug), historian->Time() );
		lg _{ _mutex };
		THROW_IFSL( _stopped, "The historian has stopped." );//during AddGroup:  the group goes with the historian when this lets go of it.
		_groups.insert_or_assign( row.Id, host );
		return host;
	}
	α HistHost::Remove( HistGroupPK id, optional<Opc::Hist::Writer> by, SL sl )ι->sp<HistGroupHost>{
		sp<HistGroupHost> host; sp<Opc::Hist::Historian> historian;
		{
			lg _{ _mutex };
			if( auto p = _groups.find(id); p!=_groups.end() ){
				host = move( p->second );
				_groups.erase( p );
			}
			historian = _historian;//set while _groups holds anything:  Stop clears both under this lock.
		}
		if( !host )
			return host;
		try{
			historian->RemoveGroup( host->Guid(), move(by), sl );
		}
		catch( const runtime_error& e ){
			ERR( "[{}]Could not remove the group:  {}", host->Guid(), e.what() );
		}
		host->Close();
		return host;
	}

	α HistHost::Loading( AnyVoidAwait& await )ι->VoidTask{
		try{
			auto groups = co_await Any( SelectHistGroups() );
			auto nodes = co_await Any( SelectHistGroupNodes() );
			auto members = HistTemplateNodesById( co_await Any(SelectHistTemplateNodes()) );
			auto connections = co_await Any( ServerCnnctnAwait{nullopt, true} );
			flat_map<ServerCnnctnPK,string> slugs;
			for( auto& connection : connections )
				slugs.emplace( connection.Id, move(connection.Slug) );
			uint loaded{};
			for( let& group : groups ){
				vector<HistGroupNodeRow> own;
				for( let& node : nodes ){
					if( node.Group==group.Id )
						own.push_back( node );
				}
				let slug = slugs.find( group.Connection );
				if( slug==slugs.end() ){
					ERR( "[{}]Group '{}' names connection {}, which doesn't exist - not loaded.", group.Guid, group.Name, group.Connection );
					continue;
				}
				try{
					Load( group, own, members, slug->second, SRCE_CUR )->Subscribe();
					++loaded;
				}
				catch( const runtime_error& e ){
					ERR( "[{}]Group '{}' could not be loaded:  {}", group.Guid, group.Name, e.what() );
				}
			}
			INFO( "Historian:  {} of {} groups loaded, {} nodes.", loaded, groups.size(), nodes.size() );
			await.Resume();
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}

	α HistHost::Resolving( HistResolveAwait& await )ι->VoidTask{
		try{
			vector<HistResolved> y;
			auto groups = co_await Any( SelectHistGroups({.Key=DB::Key{(uint)await.Group}, .IncludeDeleted=true}) );
			if( groups.empty() )
				throw Exception{ await.Source(), ExceptionArgs{EHttpStatus::NotFound}, "Group {} not found.", await.Group };
			let& group = groups.front();
			if( !group.Template ){
				await.Resume( move(y) );
				co_return;
			}
			auto members = co_await Any( SelectHistTemplateNodes({.Template=group.Template}) );
			auto existing = co_await Any( SelectHistGroupNodes({.Group=group.Id}) );
			flat_map<HistTemplateNodePK,const HistGroupNodeRow*> resolved;
			for( let& row : existing ){
				if( row.TemplateNode )
					resolved.emplace( *row.TemplateNode, &row );
			}
			vector<uint> pending;
			y.reserve( members.size() );
			for( uint i=0; i<members.size(); ++i ){
				let& member = members[i];
				if( auto p = resolved.find(member.Id); p!=resolved.end() )
					y.push_back( {member.Id, member.Path, p->second->Node, UA_STATUSCODE_GOOD} );
				else{
					y.push_back( {member.Id, member.Path, nullopt, UA_STATUSCODE_BADNOMATCH} );
					pending.push_back( i );
				}
			}
			if( pending.size() ){
				let slug = co_await SlugAwait{ group.Connection };
				sp<UAClient> client; Namespaces namespaces;
				try{
					ConnectAwait connect{ string{slug}, Collector(slug) };//an lvalue:  ConnectAwait is neither copied nor moved.
					client = co_await Any( connect );
					namespaces = co_await Any( NamespacesAwait{client} );
				}
				catch( const runtime_error& e ){
					let ua = dynamic_cast<const UAException*>( &e );
					let status = ua && ua->HasCode() ? (StatusCode)ua->Code() : (StatusCode)UA_STATUSCODE_BADSERVERNOTCONNECTED;
					WARN( "[{}]{} members of template {} can't be resolved on '{}':  {}", group.Guid, pending.size(), *group.Template, slug, e.what() );
					for( let i : pending )
						y[i].Status = status;
					await.Resume( move(y) );
					co_return;
				}
				let root = group.Root ? namespaces.ToNodeId( *group.Root ) : optional<NodeId>{ NodeId::ObjectsFolder() };
				if( !root )
					WARN( "[{}]The root '{}' names a namespace '{}' doesn't have.", group.Guid, group.Root->ToString(), slug );
				vector<UABrowsePath> paths; vector<uint> pathMembers;
				for( let i : pending ){
					auto& result = y[i];
					if( !root ){
						result.Status = UA_STATUSCODE_BADNODEIDUNKNOWN;
						continue;
					}
					vector<BrowseSegment> segments; bool spelled{ true };
					for( let& segment : ParseHistPath(result.Path, SRCE_CUR) ){
						let ns = namespaces.Index( segment.Uri );
						if( !ns ){
							WARN( "[{}]Template member {} names namespace '{}', which '{}' doesn't have.", group.Guid, result.TemplateNode, segment.Uri, slug );
							spelled = false;
							break;
						}
						segments.push_back( {*ns, segment.Name} );
					}
					if( !spelled ){
						result.Status = UA_STATUSCODE_BADNOMATCH;
						continue;
					}
					paths.emplace_back( segments, *root );
					pathMembers.push_back( i );
				}
				if( paths.size() ){
					TranslateBrowsePathsAwait translate{ move(paths), client };
					auto answers = co_await Any( translate );
					for( uint k=0; k<answers.size(); ++k ){
						auto& result = y[pathMembers[k]];
						let& answer = answers[k];
						if( !answer ){
							result.Status = answer.error();
							INFO( "[{}]Template member {} {} doesn't resolve from '{}':  {}.", group.Guid, result.TemplateNode, result.Path, root->ToString(), UAException::Message(answer.error()) );
							continue;
						}
						auto node = answer->namespaceUri.length ? optional<ExNodeId>{ answer.value() } : namespaces.ToExNodeId( answer->nodeId );
						if( !node ){
							result.Status = UA_STATUSCODE_BADNODEIDUNKNOWN;
							continue;
						}
						let text = HistNodeText( *node );
						optional<string> conflict;//the create's error, for the row that holds the node to explain:  no co_await in a handler.
						try{
							auto create = QL().Query( "createHistGroupNode( histGroupId:$group, serverConnectionId:$connection, node:$node, histTemplateNodeId:$member ){ id }",
								jobject{ {"group", group.Id}, {"connection", group.Connection}, {"node", text}, {"member", result.TemplateNode} }, await.By );
							co_await Any( *create );
							result.Node = move( *node );
							result.Status = UA_STATUSCODE_GOOD;
						}
						catch( const runtime_error& e ){//the member's status, not the resolve's:  a group create's row is already written.
							let p = dynamic_cast<const Exception*>( &e );
							let http = p ? p->HttpStatus() : EHttpStatus::InternalServerError;//HttpStatus, not _statusCode:  a DBException derives its 409.
							if( http==EHttpStatus::Conflict )
								conflict = e.what();
							else{
								result.Status = http==EHttpStatus::Unauthorized || http==EHttpStatus::Forbidden ? UA_STATUSCODE_BADUSERACCESSDENIED : UA_STATUSCODE_BADINTERNALERROR;
								WARN( "[{}]Template member {} resolved to '{}', and createHistGroupNode failed:  {}", group.Guid, result.TemplateNode, node->ToString(), e.what() );
							}
						}
						//A Conflict is the unique index, a foreign key - the group or member purged meanwhile - or another constraint, and the
						//index can be this group's own:  another member, an ad hoc row, or a concurrent resolve of this one.
						if( conflict ){
							auto holders = co_await Any( SelectHistGroupNodes({.Connection=group.Connection, .Node=text}) );
							if( holders.empty() ){
								result.Status = UA_STATUSCODE_BADINTERNALERROR;
								WARN( "[{}]Template member {} resolved to '{}', and createHistGroupNode failed:  {}", group.Guid, result.TemplateNode, node->ToString(), *conflict );
							}
							else if( let& holder = holders.front(); holder.Group!=group.Id ){
								result.Status = UA_STATUSCODE_BADNODEIDEXISTS;
								result.Holder = holder.Group;
								WARN( "[{}]Template member {} resolved to '{}', which group {} holds.", group.Guid, result.TemplateNode, node->ToString(), holder.Group );
							}
							else if( holder.TemplateNode==result.TemplateNode ){//a concurrent resolve made the row.
								result.Node = move( *node );
								result.Status = UA_STATUSCODE_GOOD;
							}
							else{
								result.Status = UA_STATUSCODE_BADNODEIDEXISTS;
								result.Holder = group.Id;
								WARN( "[{}]Template member {} resolved to '{}', which this group holds as {}.", group.Guid, result.TemplateNode, node->ToString(), holder.TemplateNode ? Ƒ("member {}", *holder.TemplateNode) : string{"an ad hoc node"} );
							}
						}
					}
				}
			}
			//The holders' names and guids, deleted groups among them:  a deleted group keeps its rows.  This group's are its row's;
			//another's are asked once, by its id, rather than every group on the gateway read for one or two.
			flat_map<HistGroupPK,std::pair<string,string>> holderRows{ {group.Id, {group.Name, group.Guid}} };
			for( auto& r : y ){
				if( !r.Holder )
					continue;
				auto holder = holderRows.find( *r.Holder );
				if( holder==holderRows.end() ){
					auto holders = co_await Any( SelectHistGroups({.Key=DB::Key{(uint)*r.Holder}, .IncludeDeleted=true}) );
					holder = holderRows.emplace( *r.Holder, holders.size() ? std::pair{holders.front().Name, holders.front().Guid} : std::pair<string,string>{} ).first;
				}
				r.HolderName = holder->second.first;
				r.HolderGuid = holder->second.second;
			}
			await.Resume( move(y) );
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}

	Ω summarize( sv guid, const vector<HistResolved>& resolved )ι->void{
		let good = std::ranges::count_if( resolved, []( let& r ){ return r.Status==UA_STATUSCODE_GOOD; } );
		if( resolved.size() )
			INFO( "[{}]{} of {} template members resolved.", guid, good, resolved.size() );
	}
	α HistHost::GroupCreated( HistGroupPK id, UserPK by, HistWork& await )ι->VoidTask{
		try{
			string guid;
			if( Enabled() ){
				auto rows = co_await Any( SelectHistGroups({.Key=DB::Key{(uint)id}}) );
				THROW_IF( rows.empty(), "hist_groups row {} not found.", id );
				let& row = rows.front();
				let slug = co_await SlugAwait{ row.Connection };
				Load( row, {}, {}, slug, SRCE_CUR );
				guid = row.Guid;
				INFO( "[{}]Group '{}' created on '{}'.", row.Guid, row.Name, slug );
			}
			summarize( guid, co_await Resolve(id, by) );
			await.Done();
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}
	α HistHost::GroupEdited( DB::Key key, HistWork& await )ι->VoidTask{
		try{
			auto rows = co_await Any( SelectHistGroups({.Key=key, .IncludeDeleted=true}) );
			auto host = rows.size() ? Find( rows.front().Id ) : nullptr;
			if( host ){
				let& row = rows.front();
				host->SetName( row.Name );
				host->SetColumns( row.Thresholds );
				auto nodes = co_await Any( SelectHistGroupNodes({.Group=row.Id}) );
				auto members = row.Template ? HistTemplateNodesById( co_await Any(SelectHistTemplateNodes({.Template=row.Template})) ) : flat_map<HistTemplateNodePK,HistTemplateNodeRow>{};
				auto thresholds = Reserve<std::pair<HistGroupNodePK,Thresholds>>( nodes.size() );
				for( let& node : nodes )
					thresholds.emplace_back( node.Id, ResolveThresholds(node.Thresholds, memberOf(members, node.TemplateNode), row.Thresholds) );
				host->SetNodeThresholds( move(thresholds), SRCE_CUR );
			}
			await.Done();
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}
	α HistHost::GroupDeleted( DB::Key key, UserPK by, HistWork& await )ι->VoidTask{
		try{
			auto rows = co_await Any( SelectHistGroups({.Key=key, .IncludeDeleted=true}) );
			if( auto host = rows.size() ? Remove( rows.front().Id, Writer(by), SRCE_CUR ) : nullptr; host ){
				co_await host->Group()->Settled();//its last flush, so a restore can add the group again.
				INFO( "[{}]Group '{}' deleted.", host->Guid(), rows.front().Name );
			}
			await.Done();
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}
	α HistHost::GroupRestored( DB::Key key, UserPK by, HistWork& await )ι->VoidTask{
		try{
			auto rows = co_await Any( SelectHistGroups({.Key=key}) );
			THROW_IF( rows.empty(), "hist_groups row '{}' not found.", key.IsPK() ? std::to_string(key.PK()) : key.NK() );
			let& row = rows.front();
			if( Enabled() ){
				auto nodes = co_await Any( SelectHistGroupNodes({.Group=row.Id}) );
				auto members = row.Template ? HistTemplateNodesById( co_await Any(SelectHistTemplateNodes({.Template=row.Template})) ) : flat_map<HistTemplateNodePK,HistTemplateNodeRow>{};
				let slug = co_await SlugAwait{ row.Connection };
				Load( row, nodes, members, slug, SRCE_CUR )->Subscribe();
				INFO( "[{}]Group '{}' restored with {} nodes.", row.Guid, row.Name, nodes.size() );
			}
			summarize( row.Guid, co_await Resolve(row.Id, by) );//members its template gained while it was deleted:  TemplateNodeCreated resolves live groups only.
			await.Done();
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}
	α HistHost::GroupPurging( HistGroupPK id, UserPK by, HistWork& await )ι->VoidTask{
		try{
			//Its own access row and the grants on it go first, through the AppServer as the resource sync writes its rows (#357):  keyed by
			//the guid, nothing else would ever purge them - the Resources page lists no criteria row - and nothing of the group goes until
			//the AppServer has answered.  A deleted group is purged too, so the guid is the row's, not a host's;  most groups have no row
			//of their own, which the AppServer answers NotFound.  No purged event exists, so the authorizers keep the row until their next
			//load:  nothing asks with its guid again.
			auto rows = co_await Any( SelectHistGroups({.Key=DB::Key{(uint)id}, .IncludeDeleted=true}) );
			if( rows.size() ){
				let& guid = rows.front().Guid;
				auto purge = AppClient()->Query<jvalue>( "purgeResource( schemaName:\"gateway\", slug:\"histGroups\", criteria:$criteria )", {{"criteria", guid}} );
				try{
					co_await Any( *purge );
					INFO( "[{}]The group's access row purged with it.", guid );
				}
				catch( const Exception& e ){
					if( e.HttpStatus()!=EHttpStatus::NotFound )
						throw;
				}
			}
			if( auto host = Remove(id, Writer(by), SRCE_CUR); host )
				co_await host->Group()->Settled();
			let view = GetViewPtr( "hist_group_nodes" );
			co_await Any( DS()->Execute(DB::Sql{Ƒ("delete from {} where hist_group_id=?", view->SqlName()), {DB::Value{(uint)id}}}) );
			await.Done();
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}
	α HistHost::NodeCreated( HistGroupNodePK id, UserPK by, HistWork& await )ι->VoidTask{
		try{
			auto rows = co_await Any( SelectHistGroupNodes({.Id=id}) );
			THROW_IF( rows.empty(), "hist_group_nodes row {} not found.", id );
			let& row = rows.front();
			if( auto host = Find(row.Group); host ){
				auto members = row.TemplateNode ? HistTemplateNodesById( co_await Any(SelectHistTemplateNodes({.Id=row.TemplateNode})) ) : flat_map<HistTemplateNodePK,HistTemplateNodeRow>{};
				host->Join( row, ResolveThresholds(row.Thresholds, memberOf(members, row.TemplateNode), host->Columns()), Writer(by), SRCE_CUR );
			}
			await.Done();
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}
	α HistHost::NodeEdited( HistGroupNodePK id, HistWork& await )ι->VoidTask{
		try{
			auto rows = co_await Any( SelectHistGroupNodes({.Id=id}) );
			if( auto host = rows.size() ? Find( rows.front().Group ) : nullptr; host ){
				let& row = rows.front();
				auto members = row.TemplateNode ? HistTemplateNodesById( co_await Any(SelectHistTemplateNodes({.Id=row.TemplateNode})) ) : flat_map<HistTemplateNodePK,HistTemplateNodeRow>{};
				host->SetNodeThresholds( {{row.Id, ResolveThresholds(row.Thresholds, memberOf(members, row.TemplateNode), host->Columns())}}, SRCE_CUR );
			}
			await.Done();
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}
	α HistHost::NodePurging( HistGroupNodePK id, UserPK by, HistWork& await )ι->VoidTask{
		try{
			auto rows = co_await Any( SelectHistGroupNodes({.Id=id}) );
			if( auto host = rows.size() ? Find( rows.front().Group ) : nullptr; host )
				host->Leave( id, Writer(by), SRCE_CUR );
			await.Done();
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}
	α HistHost::TemplateNodeCreated( HistTemplateNodePK id, UserPK by, HistWork& await )ι->VoidTask{
		try{
			auto rows = co_await Any( SelectHistTemplateNodes({.Id=id}) );
			THROW_IF( rows.empty(), "hist_template_nodes row {} not found.", id );
			auto groups = co_await Any( SelectHistGroups({.Template=rows.front().Template}) );
			for( let& group : groups )
				summarize( group.Guid, co_await Resolve(group.Id, by) );
			await.Done();
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}
	α HistHost::TemplateNodeEdited( HistTemplateNodePK id, HistWork& await )ι->VoidTask{
		try{
			auto members = HistTemplateNodesById( co_await Any(SelectHistTemplateNodes({.Id=id})) );
			auto nodes = co_await Any( SelectHistGroupNodes({.TemplateNode=id}) );
			flat_map<sp<HistGroupHost>,vector<std::pair<HistGroupNodePK,Thresholds>>> byHost;//one modify per group.
			for( let& node : nodes ){
				if( auto host = Find(node.Group); host )
					byHost[host].emplace_back( node.Id, ResolveThresholds(node.Thresholds, memberOf(members, node.TemplateNode), host->Columns()) );
			}
			for( auto&& [host,thresholds] : byHost )
				host->SetNodeThresholds( move(thresholds), SRCE_CUR );
			await.Done();
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}
	α HistHost::TemplateNodePurging( HistTemplateNodePK id, UserPK by, HistWork& await )ι->VoidTask{
		try{
			auto nodes = co_await Any( SelectHistGroupNodes({.TemplateNode=id}) );
			for( let& node : nodes ){
				auto purge = QL().Query( "purgeHistGroupNode( id:$id )", jobject{{"id", node.Id}}, by );
				co_await Any( *purge );
			}
			await.Done();
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}
	α HistHost::TemplatePurging( HistTemplatePK id, UserPK by, HistWork& await )ι->VoidTask{
		try{
			auto groups = co_await Any( SelectHistGroups({.Template=id, .IncludeDeleted=true}) );
			if( groups.size() )
				throw Exception{ await.Source(), ExceptionArgs{EHttpStatus::Conflict}, "Template {} is used by {} group{} - purge them first.", id, groups.size(), groups.size()==1 ? "" : "s" };
			auto members = co_await Any( SelectHistTemplateNodes({.Template=id}) );
			for( let& member : members ){
				auto purge = QL().Query( "purgeHistTemplateNode( id:$id )", jobject{{"id", member.Id}}, by );
				co_await Any( *purge );
			}
			await.Done();
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}
}