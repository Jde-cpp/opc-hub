#include <jde/flows/Graph.h>
#include "nodes/Nodes.h"
#include "nodes/Props.h"

#define let const auto

namespace Jde::Opc::Flows{
	namespace{
		struct TypeInfo final{ sv Name; bool Hosted; bool Source; bool Input; };
		constexpr std::array<TypeInfo,28> _types{{//in ENodeType's order.
			{"opc in", true, true, false}, {"group in", true, true, false}, {"timer", true, true, false}, {"calendar", true, true, false},
			{"connection", true, true, false}, {"inject", false, true, false}, {"link in", false, true, false},
			{"switch", false, false, true}, {"change", false, false, true}, {"filter", false, false, true}, {"range", false, false, true},
			{"delay", false, false, true}, {"trigger", false, false, true}, {"window", false, false, true}, {"split", false, false, true},
			{"join", false, false, true}, {"template", false, false, true}, {"function", false, false, true}, {"debug", false, false, true},
			{"catch", false, false, false}, {"status", false, false, false}, {"complete", false, false, false}, {"link out", false, false, true},
			{"comment", false, false, false},
			{"opc out", true, false, true}, {"variable", true, false, true}, {"log", true, false, true}, {"http request", true, false, true}
		}};
		static_assert( _types[(uint8)ENodeType::HttpRequest].Name=="http request" );
		α info( ENodeType type )ι->const TypeInfo&{ return _types[(uint8)type]; }

		struct Parsed final{ NodeDef Def; vector<vector<string>> Wires; };
		//A node's own members, with its wires still ids.  Throws its fault's message.
		α readNode( const jobject& o )ε->Parsed{
			Parsed y;
			let type = ToNodeType( Json::FindDefaultSV(o, "type") );
			THROW_IF( !type, "`type` {} is not a node type.", o.contains("type") ? serialize(o.at("type")) : "is missing, and so" );
			y.Def.Type = *type;
			if( Prop::Find(o, "name") )
				y.Def.Name = Prop::Text( o, "name" );
			if( let props = Prop::Find(o, "props") ){
				THROW_IF( !props->is_object(), "`props` must be an object." );
				y.Def.Props = props->get_object();
			}
			if( let wires = Prop::Find(o, "wires") ){
				let valid = wires->is_array() && std::ranges::all_of( wires->get_array(), []( let& port ){
					return port.is_array() && std::ranges::all_of( port.get_array(), []( let& id ){ return id.is_string(); } );
				} );
				THROW_IF( !valid, "`wires` must be a list per output port of the ids the port feeds." );
				for( let& port : wires->get_array() ){
					auto& ids = y.Wires.emplace_back();
					for( let& id : port.get_array() )
						ids.emplace_back( id.get_string() );
				}
			}
			return y;
		}
	}

	α Graph::IndexOf( sv id )Ι->optional<uint32>{
		let p = find( Nodes, id, &NodeDef::Id );
		return p==Nodes.end() ? nullopt : optional<uint32>{ p-Nodes.begin() };
	}

	α Graph::Read( const jvalue& json )ι->std::expected<Graph,Fault>{
		let root = json.if_object();
		let nodes = root ? root->if_contains( "nodes" ) : nullptr;
		if( !nodes || !nodes->is_array() )
			return std::unexpected{ Fault{{}, "A graph is an object with a `nodes` list."} };
		Graph y;
		vector<vector<vector<string>>> wires;
		for( let& item : nodes->get_array() ){
			let o = item.if_object();
			let id = o ? Json::FindSV( *o, "id" ) : nullopt;
			if( !id || id->empty() )
				return std::unexpected{ Fault{{}, Ƒ("Node {} of the graph has no `id`.", y.Nodes.size()+1)} };
			if( y.IndexOf(*id) )
				return std::unexpected{ Fault{string{*id}, "Two nodes have this id."} };
			try{
				auto parsed = readNode( *o );
				parsed.Def.Id = *id;
				y.Nodes.push_back( move(parsed.Def) );
				wires.push_back( move(parsed.Wires) );
			}
			catch( const std::exception& e ){
				return std::unexpected{ Fault{string{*id}, e.what()} };
			}
		}
		flat_map<string,uint> links;//each `link in`'s name.
		for( let& node : y.Nodes ){
			if( node.Type==ENodeType::LinkIn )
				++links[string{ Json::FindDefaultSV(node.Props, "name") }];
		}
		for( uint i=0; i<y.Nodes.size(); ++i ){
			auto& node = y.Nodes[i];
			try{
				if( !IsHosted(node.Type) )
					MakeNode( node, y );
				let name = Json::FindDefaultSV( node.Props, "name" );
				THROW_IF( node.Type==ENodeType::LinkIn && links[string{name}]>1, "Two `link in` nodes are named '{}'.", name );
				THROW_IF( node.Type==ENodeType::LinkOut && !links.contains(string{name}), "No `link in` of the flow is named '{}'.", name );
				let outputs = Outputs( node );
				for( uint port=0; port<wires[i].size(); ++port ){
					auto& targets = node.Wires.emplace_back();
					THROW_IF( port>=outputs && !wires[i][port].empty(), "Output {} is wired, and a '{}' node has {}.", port+1, ToString(node.Type), outputs );
					for( let& id : wires[i][port] ){
						let target = y.IndexOf( id );
						THROW_IF( !target, "A wire goes to '{}', which is not a node of the graph.", id );
						THROW_IF( !HasInput(y.Nodes[*target].Type), "A wire goes to '{}', and a '{}' node takes no input.", id, ToString(y.Nodes[*target].Type) );
						targets.push_back( *target );
					}
				}
			}
			catch( const std::exception& e ){
				return std::unexpected{ Fault{node.Id, e.what()} };
			}
		}
		return y;
	}
}
namespace Jde::Opc{
	α Flows::ToString( ENodeType type )ι->sv{ return (uint8)type<_types.size() ? info( type ).Name : sv{ "?" }; }
	α Flows::ToNodeType( sv name )ι->optional<ENodeType>{
		let p = find( _types, name, &TypeInfo::Name );
		return p==_types.end() ? nullopt : optional{ (ENodeType)(p-_types.begin()) };
	}
	α Flows::IsHosted( ENodeType type )ι->bool{ return info( type ).Hosted; }
	α Flows::IsSource( ENodeType type )ι->bool{ return info( type ).Source; }
	α Flows::HasInput( ENodeType type )ι->bool{ return info( type ).Input; }
}