#include "Nodes.h"
#include <jde/flows/Props.h>

#define let const auto

namespace Jde::Opc{
	α Flows::MakeNode( const NodeDef& def, const Graph& graph )ε->up<INode>{
		let& props = def.Props;
		switch( def.Type ){
		case ENodeType::Switch: return MakeSwitch( props );
		case ENodeType::Change: return MakeChange( props );
		case ENodeType::Filter: return MakeFilter( props );
		case ENodeType::Range: return MakeRange( props );
		case ENodeType::Delay: return MakeDelay( props );
		case ENodeType::Trigger: return MakeTrigger( props );
		case ENodeType::Window: return MakeWindow( props );
		case ENodeType::Split: return MakeSplit( props );
		case ENodeType::Join: return MakeJoin( props );
		case ENodeType::Template: return MakeTemplate( props );
		case ENodeType::Debug: return MakeDebug( props );
		case ENodeType::LinkIn:
		case ENodeType::LinkOut: return MakeLink( props );
		case ENodeType::Inject:{
			let message = Prop::Find( props, "message" );
			THROW_IF( message && !message->is_object(), "`message` must be an object, the message the node sends." );
			return nullptr;
		}
		case ENodeType::Catch:
			Prop::Flag( props, "uncaught" );
			[[fallthrough]];
		case ENodeType::Status:
			Scope( def, graph );
			return nullptr;
		case ENodeType::Complete:
			THROW_IF( Scope(def, graph).empty(), "`scope` must name the nodes a complete node takes from." );
			return nullptr;
		case ENodeType::Comment: return nullptr;
		case ENodeType::Function: return MakeFunction( def, MakeSandbox({}, {}, {}) );//a sandbox of its own, to check the code in.
		default: THROW( "A '{}' is its host's to make.", ToString(def.Type) );
		}
	}
	α Flows::Outputs( const NodeDef& def )ι->uint{
		switch( def.Type ){
		case ENodeType::Switch:{
			let rules = def.Props.if_contains( "rules" );
			return rules && rules->is_array() ? rules->get_array().size() : 0;
		}
		case ENodeType::Function:{
			let outputs = def.Props.if_contains( "outputs" );
			boost::system::error_code ec;
			let count = outputs ? outputs->to_number<uint>( ec ) : 1;
			return ec ? 1 : count;
		}
		case ENodeType::Debug:
		case ENodeType::LinkOut:
		case ENodeType::Comment:
		case ENodeType::Variable:
		case ENodeType::Log: return 0;
		default: return 1;
		}
	}
	α Flows::Scope( const NodeDef& def, const Graph& graph )ε->flat_set<uint32>{
		flat_set<uint32> y;
		let scope = Prop::Find( def.Props, "scope" );
		if( !scope )
			return y;
		THROW_IF( !scope->is_array(), "`scope` must be a list of node ids." );
		for( let& id : scope->get_array() ){
			let index = id.is_string() ? graph.IndexOf( id.get_string() ) : nullopt;
			THROW_IF( !index, "`scope` names {}, which is not a node of the graph.", serialize(id) );
			y.insert( *index );
		}
		return y;
	}
}