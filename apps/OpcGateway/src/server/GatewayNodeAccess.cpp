#include "GatewayNodeAccess.h"

#define let const auto
namespace Jde::Opc::Gateway{
	using Access::ERights;
	using Opc::Server::EAccess;

	α GatewayNodeAccess::Add( NodeId node, string guid )ι->void{
		ul _{ _mutex };
		_guids.insert_or_assign( move(node), move(guid) );
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
	α GatewayNodeAccess::Rights( str guid, UserPK executer )Ι->ERights{
		return _acl ? _acl->Rights( string{Schema}, string{Resource}, guid, executer ) : ERights::None;
	}
	//The mask where the group is enforced - its own row or the table's active - and None where it isn't:  CheckEnforced with the
	//mask Rights answered passes exactly then, since a denial is already out of the mask.
	α GatewayNodeAccess::Edits( str guid, UserPK executer )Ι->ERights{
		let rights = Rights( guid, executer );
		if( rights==ERights::None || !_acl )
			return ERights::None;
		return _acl->CheckEnforced( string{Schema}, string{Resource}, guid, rights, executer, SRCE_CUR ) ? rights : ERights::None;
	}

	α GatewayNodeAccess::NodeRights( const NodeId& node, UserPK executer )ι->ERights{
		let guid = Guid( node );
		return guid ? Rights( *guid, executer ) : ERights::Read;
	}
	α GatewayNodeAccess::EditRights( const NodeId& node, UserPK executer )ι->ERights{
		let guid = Guid( node );
		return guid ? Edits( *guid, executer ) : ERights::None;
	}
	α GatewayNodeAccess::MayBrowse( const NodeId& node, UserPK executer )ι->bool{
		let guid = Guid( node );
		return !guid || !empty( Rights(*guid, executer) & ERights::Read );
	}
	α GatewayNodeAccess::UserRights( const NodeId& node, UserPK executer )ι->EAccess{
		let guid = Guid( node );
		if( !guid )
			return EAccess::Read;
		constexpr uint8 historyWrite{ underlying(EAccess::HistoryWrite) };//an edit's bit, as OpcAuthorize::UserRights has it.
		return (EAccess)( (underlying(Opc::Server::ToAccess(Rights(*guid, executer))) & ~historyWrite) | (underlying(Opc::Server::ToAccess(Edits(*guid, executer))) & historyWrite) );
	}
}