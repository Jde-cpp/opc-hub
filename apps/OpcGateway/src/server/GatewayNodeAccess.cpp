#include "GatewayNodeAccess.h"

#define let const auto
namespace Jde::Opc::Gateway{
	using Access::ERights;
	using Opc::Server::EAccess;

	α GatewayNodeAccess::Add( NodeId node, string guid )ι->bool{
		ul _{ _mutex };
		return _guids.try_emplace( move(node), move(guid) ).second;
	}
	α GatewayNodeAccess::Remove( const NodeId& node )ι->void{
		ul _{ _mutex };
		_guids.erase( node );
	}
	α GatewayNodeAccess::Guid( const NodeId& node )Ι->optional<string>{
		rl _{ _mutex };
		auto p = _guids.find( node );
		return p==_guids.end() ? nullopt : optional<string>{ p->second };
	}
	//Without an authorizer there is no acl to ask, and a served node is closed rather than open:  the gateway's starts before the endpoint.
	α GatewayNodeAccess::Rights( str guid, UserPK executer )Ι->Access::Authorize::ScopeRights{
		return _acl ? _acl->ScopedRights( Schema, Resource, guid, executer ) : Access::Authorize::ScopeRights{ ERights::None, false };
	}
	//The mask where the group is enforced - its own row or the table's active - and None where it isn't, as CheckEnforced passes an edit.
	Ω edits( const Access::Authorize::ScopeRights& rights )ι->ERights{ return rights.Enforced ? rights.Rights : ERights::None; }

	α GatewayNodeAccess::NodeRights( const NodeId& node, UserPK executer )ι->ERights{
		let guid = Guid( node );
		return guid ? Rights( *guid, executer ).Rights : ERights::Read;
	}
	α GatewayNodeAccess::EditRights( const NodeId& node, UserPK executer )ι->ERights{
		let guid = Guid( node );
		return guid ? edits( Rights(*guid, executer) ) : ERights::None;
	}
	α GatewayNodeAccess::MayBrowse( const NodeId& node, UserPK executer )ι->bool{
		let guid = Guid( node );
		return !guid || !empty( Rights(*guid, executer).Rights & ERights::Read );
	}
	α GatewayNodeAccess::UserRights( const NodeId& node, UserPK executer )ι->EAccess{
		let guid = Guid( node );
		if( !guid )
			return EAccess::Read;
		let rights = Rights( *guid, executer );//once:  the mask and its enforcement from the same look, so a row enforced meanwhile can't pair them up wrong.
		constexpr uint8 historyWrite{ underlying(EAccess::HistoryWrite) };//an edit's bit, as OpcAuthorize::UserRights has it.
		//Never granted until Phase 5 forwards a write to the source (then an edit's bit, as HistoryWrite):  until then the variable would
		//store it as the served value.  The AccessLevel keeps the source's.
		constexpr uint8 write{ underlying(EAccess::Write) };
		return (EAccess)( (underlying(Opc::Server::ToAccess(rights.Rights)) & ~(historyWrite | write)) | (underlying(Opc::Server::ToAccess(edits(rights))) & historyWrite) );
	}
}