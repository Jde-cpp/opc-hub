#include "HistGroupQLAwait.h"
#include <boost/uuid/random_generator.hpp>
#include <jde/fwk/str.h>
#include <jde/access/Authorize.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/ops/MutationAwait.h>
#include "../async/ConnectAwait.h"
#include "../async/NamespacesAwait.h"
#include "../GatewayAppClient.h"
#include "../hist/HistHost.h"
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	using QL::EMutationQL;
	using namespace HistTable;

	Ω histTable( const QL::MutationQL& m )ι->bool{
		let table = m.TableName();
		return table==Groups || table==GroupNodes || table==Templates || table==TemplateNodes;
	}
	α HistGroupMAwait::IsApplicable( const QL::MutationQL& m )ι->bool{
		using enum EMutationQL;
		return m.CommandName==ResolveCommand || ( histTable(m) && (m.Type==Create || m.Type==Update || m.Type==Delete || m.Type==Restore || m.Type==Purge) );
	}
	α HistGroupMAwait::Test( QL::MutationQL& m, QL::Creds executer, SL sl )->up<TAwait<jvalue>>{
		return IsApplicable( m ) ? mu<HistGroupMAwait>( move(m), move(executer), sl ) : nullptr;
	}

	[[noreturn]] Ω refuse( string what, SL sl )ε->void{ throw Exception{ sl, ExceptionArgs{EHttpStatus::BadRequest}, "{}", move(what) }; }
	//Whether `by` may read the group its guid keys (spec *Authorization*), as historyGroups asks before it lists one
	//(HistGroupInfoQLAwait):  a refusal or an answer names the group that holds a node only to a caller who may read it.  The rest
	//learn its id, which says nothing of the group.
	Ω mayRead( str guid, UserPK by, SL sl )ε->bool{
		return AppClient()->Acl()->Check( "gateway", "histGroups", guid, Access::ERights::Read, by, sl ).has_value();
	}

	//The right the stock op asks, which this asks first:  a create spells its node through the server, and a purge's live side
	//runs before the statement.
	Ω rights( EMutationQL type )ι->Access::ERights{
		using enum EMutationQL;
		switch( type ){
		case Create: return Access::ERights::Create;
		case Update: return Access::ERights::Update;
		case Delete: case Restore: return Access::ERights::Delete;
		case Purge: return Access::ERights::Purge;
		default: return Access::ERights::None;
		}
	}
	Ω rowId( const QL::MutationQL& m, SL sl )ε->uint32{
		let id = m.FindId<uint32>();
		if( !id )
			refuse( Ƒ("'{}' needs the row's id.", m.CommandName), sl );
		return *id;
	}
	//What the library or the tables would refuse, before anything is written.
	Ω check( const QL::MutationQL& m, SL sl )ε->void{
		using enum EMutationQL;
		if( m.Type!=Create && m.Type!=Update )
			return;
		let table = m.TableName();
		let args = m.ExtrapolateVariables();
		HistThresholdColumns::FromJson( args, sl ).Check( sl );
		if( m.Type==Create ){
			let require = [&]( sv name ){
				if( let p = args.if_contains(name); !p || p->is_null() )
					refuse( Ƒ("{} is required.", name), sl );
			};
			let requireId = [&]( sv name ){
				require( name );
				if( let& id = args.at(name); !id.try_to_number<uint32>() )
					refuse( Ƒ("{} is a row's id, not {}.", name, serialize(id)), sl );
			};
			if( table==Groups )
				requireId( "serverConnectionId" );
			else if( table==GroupNodes ){
				requireId( "histGroupId" );
				require( "node" );
			}
			else if( table==TemplateNodes ){
				require( "path" );
				ParseHistPath( Json::AsSV(args, "path"), sl );
			}
		}
		else{
			vector<sv> fixed;
			if( table==Groups )
				fixed = { "guid", "serverConnectionId", "histTemplateId", "root" };
			else if( table==GroupNodes )
				fixed = { "node", "histGroupId", "serverConnectionId", "histTemplateNodeId" };
			else if( table==TemplateNodes )
				fixed = { "path", "histTemplateId" };
			for( let name : fixed ){
				if( args.contains(name) )
					refuse( Ƒ("{} can't change:  purge the row and create it again.", name), sl );
			}
		}
	}
	//A purge's live side, which runs before the statement:  the row's children and its group's files go first.  guid:  a group's,
	//read while its row is here, for after.
	Ω before( const QL::MutationQL& m, UserPK by, string& guid, SL sl )ε->optional<HistWork::Work>{
		if( m.Type!=EMutationQL::Purge )
			return nullopt;
		auto& host = HistHost::Instance();
		let table = m.TableName();
		let pk = rowId( m, sl );
		if( table==Groups )
			return HistWork::Work{ [pk, by, &guid, &host]( HistWork& w ){ return host.GroupPurging( pk, by, guid, w ); } };
		if( table==GroupNodes )
			return HistWork::Work{ [pk, by, &host]( HistWork& w ){ return host.NodePurging( pk, by, w ); } };
		if( table==TemplateNodes )
			return HistWork::Work{ [pk, by, &host]( HistWork& w ){ return host.TemplateNodePurging( pk, by, w ); } };
		return HistWork::Work{ [pk, by, &host]( HistWork& w ){ return host.TemplatePurging( pk, by, w ); } };
	}
	//The live side of a row written:  result is the stock op's, an insert's new id among it;  guid, a purged group's (before).
	Ω after( const QL::MutationQL& m, const jvalue& result, UserPK by, str guid, SL sl )ε->optional<HistWork::Work>{
		using enum EMutationQL;
		auto& host = HistHost::Instance();
		let table = m.TableName();
		if( m.Type==Purge ){
			if( table==Groups && guid.size() )
				return HistWork::Work{ [guid, &host]( HistWork& w ){ return host.GroupPurged( guid, w ); } };
		}
		else if( m.Type==Create ){
			let row = result.is_array() && result.get_array().size() ? result.get_array()[0].if_object() : nullptr;
			let pk = row ? Json::FindNumber<uint32>( *row, "id" ).value_or( 0 ) : 0;
			THROW_IFSL( !pk && table!=Templates, "'{}' answered no id:  {}", m.CommandName, serialize(result) );
			if( table==Groups )
				return HistWork::Work{ [pk, by, &host]( HistWork& w ){ return host.GroupCreated( pk, by, w ); } };
			if( table==GroupNodes )
				return HistWork::Work{ [pk, by, &host]( HistWork& w ){ return host.NodeCreated( pk, by, w ); } };
			if( table==TemplateNodes )
				return HistWork::Work{ [pk, by, &host]( HistWork& w ){ return host.TemplateNodeCreated( pk, by, w ); } };
		}
		else if( table==Groups ){
			auto key = m.GetKey( sl );
			if( m.Type==Update )
				return HistWork::Work{ [key, &host]( HistWork& w ){ return host.GroupEdited( key, w ); } };
			if( m.Type==Delete )
				return HistWork::Work{ [key, by, &host]( HistWork& w ){ return host.GroupDeleted( key, by, w ); } };
			if( m.Type==Restore )
				return HistWork::Work{ [key, by, &host]( HistWork& w ){ return host.GroupRestored( key, by, w ); } };
		}
		else if( m.Type==Update && table==GroupNodes ){
			let pk = rowId( m, sl );
			return HistWork::Work{ [pk, &host]( HistWork& w ){ return host.NodeEdited( pk, w ); } };
		}
		else if( m.Type==Update && table==TemplateNodes ){
			let pk = rowId( m, sl );
			return HistWork::Work{ [pk, &host]( HistWork& w ){ return host.TemplateNodeEdited( pk, w ); } };
		}
		return nullopt;
	}

	//The spelling a node column holds:  the caller's node, or with its index turned into the URI the connection's server has
	//for it, which takes the collector's client.
	struct SpellAwait final : AnyAwait<string>{
		SpellAwait( ExNodeId node, ServerCnnctnPK connection, SRCE )ι:AnyAwait<string>{ sl }, _node{ move(node) }, _connection{ connection }{}
	protected:
		α Suspend()ι->void override{ Execute(); }
	private:
		α Execute()ι->VoidTask{
			try{
				let slug = co_await SlugAwait{ _connection, _sl };
				ConnectAwait connect{ string{slug}, HistHost::Instance().Collector(slug) };//an lvalue:  ConnectAwait is neither copied nor moved.
				auto client = co_await Any( connect );
				let namespaces = co_await Any( NamespacesAwait{client} );
				let expanded = namespaces.ToExNodeId( _node.nodeId );
				if( !expanded )
					throw Exception{ _sl, ExceptionArgs{EHttpStatus::BadRequest}, "'{}' names namespace index {}, which '{}' doesn't have.", _node.ToString(), _node.NsIndex(), slug };
				Resume( HistNodeText(*expanded) );
			}
			catch( runtime_error& e ){
				ResumeExp( move(e) );
			}
		}
		ExNodeId _node;
		ServerCnnctnPK _connection;
	};

	Ω toJson( const optional<QL::TableQL>& request, const vector<HistResolved>& resolved )ι->jvalue{
		jobject y;
		if( let nodesQL = request ? request->FindTable("nodes") : nullptr; nodesQL ){
			let wantMember = nodesQL->FindColumn( "templateNode" ), wantPath = nodesQL->FindColumn( "path" ), wantStatus = nodesQL->FindColumn( "status" );
			let wantNode = nodesQL->FindColumn( "node" ) || nodesQL->FindTable( "node" );
			let holderQL = nodesQL->FindTable( "holder" );
			let wantHolder = holderQL || nodesQL->FindColumn( "holder" );
			jarray rows; rows.reserve( resolved.size() );
			for( let& r : resolved ){
				jobject row;
				if( wantMember )
					row["templateNode"] = r.TemplateNode;
				if( wantPath )
					row["path"] = r.Path;
				if( wantNode )
					row["node"] = r.Node ? jvalue{ r.Node->ToJson() } : jvalue{};
				if( wantStatus )
					row["status"] = r.Status;
				if( wantHolder ){
					jvalue holder;
					if( r.Holder ){
						jobject h;
						if( !holderQL || holderQL->FindColumn("id") )
							h["id"] = *r.Holder;
						if( !holderQL || holderQL->FindColumn("name") )
							h["name"] = r.HolderName.size() ? jvalue{ r.HolderName } : jvalue{};
						holder = move( h );
					}
					row["holder"] = move( holder );
				}
				rows.push_back( move(row) );
			}
			y["nodes"] = move( rows );
		}
		return y;
	}

	α HistGroupMAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			jvalue y;
			if( _query.CommandName==ResolveCommand ){
				let group = _query.AsNumber<HistGroupPK>( "group", _sl );
				//The gate, as HistHost::Gate keys it, by the row rather than a host:  a deleted group resolves too, as does one without the
				//historian.  A group that doesn't exist is gated by the table's row, as one with no row of its own is, and Resolve then refuses it.
				auto groups = co_await Any( SelectHistGroups({.Key=DB::Key{(uint)group}, .IncludeDeleted=true}) );
				AppClient()->Acl()->Test( "gateway", "histGroups", groups.empty() ? string{} : groups.front().Guid, Access::ERights::Update, UserPK(), _sl );
				auto resolved = co_await HistHost::Instance().Resolve( group, UserPK(), _sl );
				for( auto& r : resolved ){
					if( r.Holder && !mayRead(r.HolderGuid, UserPK(), _sl) )
						r.HolderName.clear();
				}
				y = toJson( _query.ResultRequest, resolved );
			}
			else{
				_query.DBTable->Authorize( rights(_query.Type), UserPK(), _sl );
				check( _query, _sl );
				jobject spelled;//the columns spelled here, which the result carries over what the caller sent.
				optional<HistGroupRow> nodeGroup;//a hist_group_nodes create's group, for a refusal to say which group holds the node.
				if( _query.Type==EMutationQL::Create && _query.TableName()==Groups ){
					let connection = _query.AsNumber<ServerCnnctnPK>( "serverConnectionId", _sl );
					spelled["guid"] = Jde::ToString( boost::uuids::random_generator{}() );
					if( let root = _query.FindPtr("root"); root && !root->is_null() ){
						auto node = ParseHistNode( *root, _sl );
						spelled["root"] = node.namespaceUri.length ? HistNodeText( node ) : co_await SpellAwait{ move(node), connection, _sl };
					}
				}
				else if( _query.Type==EMutationQL::Create && _query.TableName()==GroupNodes ){
					let groupId = _query.AsNumber<HistGroupPK>( "histGroupId", _sl );
					auto groups = co_await Any( SelectHistGroups({.Key=DB::Key{(uint)groupId}, .IncludeDeleted=true}) );
					if( groups.empty() )
						throw Exception{ _sl, ExceptionArgs{EHttpStatus::NotFound}, "Group {} not found.", groupId };
					let& group = groups.front();
					if( let connection = _query.TryNumber<ServerCnnctnPK>("serverConnectionId"); connection && *connection!=group.Connection )
						refuse( Ƒ("serverConnectionId {} isn't group {}'s, {}:  a group never spans servers.", *connection, groupId, group.Connection), _sl );
					spelled["serverConnectionId"] = group.Connection;
					auto node = ParseHistNode( _query.As<jvalue>("node", _sl), _sl );
					spelled["node"] = node.namespaceUri.length ? HistNodeText( node ) : co_await SpellAwait{ move(node), group.Connection, _sl };
					nodeGroup = group;
				}
				else if( _query.Type==EMutationQL::Create && _query.TableName()==TemplateNodes )//one spelling, so (hist_template_id, path) is the member's key.
					spelled["path"] = HistPathText( ParseHistPath(_query.As<jstring>("path", _sl), _sl) );
				for( let& [key,value] : spelled )
					_query.Args[key] = value;

				string purgedGuid;
				if( auto work = before(_query, UserPK(), purgedGuid, _sl); work )
					co_await HistWork{ std::move(*work), _sl };
				std::exception_ptr conflict;//a hist_group_nodes create's 409, for the row that holds the node to explain:  no co_await in a handler.
				try{
					y = co_await QL::MutationAwait{ QL::MutationQL{_query}, _creds, nullptr, _sl };//the stock op:  no QL, so nothing routes it back here.
				}
				catch( const runtime_error& e ){
					let p = dynamic_cast<const Exception*>( &e );
					if( !nodeGroup || !p || p->HttpStatus()!=EHttpStatus::Conflict )//HttpStatus, not _statusCode:  a DBException derives its 409.
						throw;
					conflict = std::current_exception();
				}
				//The unique (connection, node) index, asked once it has refused the row, so the refusal names the group that holds the node
				//(spec *Templates*) where the database's names the index - a create that succeeds asks nothing, and one that lost a race to a
				//concurrent create is explained too.  A Conflict no row explains, a foreign key or another constraint, is the database's.
				if( conflict ){
					let& group = *nodeGroup;
					let text = Json::AsSV( spelled, "node" );
					let found = co_await SelectNodeHolder( group, string{text} );
					if( found.Kind==NodeHolder::EKind::None )
						std::rethrow_exception( conflict );
					let& holder = *found.Row;
					if( found.Kind!=NodeHolder::EKind::OtherGroup )
						throw Exception{ _sl, {EHttpStatus::Conflict}, "'{}' is already in this group{} ({}), as {} (row {}).", text, mayRead(group.Guid, UserPK(), _sl) ? Ƒ(", '{}'", group.Name) : string{}, group.Id, holder.TemplateNode ? Ƒ("template member {}", *holder.TemplateNode) : string{"an ad hoc node"}, holder.Id };
					auto holderGroups = co_await Any( SelectHistGroups({.Key=DB::Key{(uint)holder.Group}, .IncludeDeleted=true}) );//its guid keys its access row;  one purged meanwhile has no row, and no name to give.
					if( holderGroups.empty() || !mayRead(holderGroups.front().Guid, UserPK(), _sl) )
						throw Exception{ _sl, {EHttpStatus::Conflict}, "'{}' is already in group {} on this connection:  a node belongs to one group per server.", text, holder.Group };
					throw Exception{ _sl, {EHttpStatus::Conflict}, "'{}' is already in group '{}' ({}) on this connection:  a node belongs to one group per server.{}", text, holderGroups.front().Name, holder.Group, holderGroups.front().Deleted ? "  That group is deleted:  restore it, or purge it to free its nodes." : "" };
				}
				if( auto work = after(_query, y, UserPK(), purgedGuid, _sl); work )
					co_await HistWork{ std::move(*work), _sl };
				if( auto row = spelled.size() && y.is_array() && y.get_array().size() ? y.get_array()[0].if_object() : nullptr; row ){
					for( let& [key,value] : spelled )
						(*row)[key] = value;
				}
			}
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}