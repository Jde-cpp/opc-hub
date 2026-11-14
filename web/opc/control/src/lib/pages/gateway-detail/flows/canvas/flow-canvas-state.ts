import type { IFStateConnection, IFStateData, IFStateNode } from '@foblex/flow';
import { FlowGraph } from '../../../../model/flow';
import { flowOutputs } from '../../../../model/flow-nodes';

//The graph (docs/flows/spec.md *Flows*) against the canvas library's records (#446):  a node's wires are a list of target ids
//per output port, where the library joins two connector ids with a record of its own.  The port goes ahead of the node in a
//connector's id, since a node's id is the graph's and may hold anything.
export interface CanvasNode extends IFStateNode{ type:string; name:string; props:Record<string,unknown>; }
export type CanvasWire = IFStateConnection;

export const inId = ( node:string )=>`in:${node}`;
export const outId = ( node:string, port:number )=>`out:${port}:${node}`;
export const wireId = ( source:string, target:string )=>`${source}>${target}`;
export function outPort( connector:string ):{ node:string; port:number }{
	const at = connector.indexOf( ':', 4 );
	return { port: Number( connector.substring( 4, at ) ), node: connector.substring( at+1 ) };
}
export const inNode = ( connector:string )=>connector.substring( 3 );
export const wire = ( node:string, port:number, target:string ):CanvasWire=>({ id: wireId( outId( node, port ), inId( target ) ), sourceId: outId( node, port ), targetId: inId( target ) });

export function toCanvas( graph:FlowGraph ):Partial<IFStateData<CanvasNode,CanvasWire>>{
	const nodes = graph.nodes.map( x=>({ id: x.id, position: {x: x.x, y: x.y}, type: x.type, name: x.name, props: x.props }) );
	const ids = new Set( graph.nodes.map( x=>x.id ) );//a wire to no node of the graph has no connector to end at
	const connections = graph.nodes.flatMap( x=>x.wires.flatMap( ( targets, port )=>targets.filter( t=>ids.has( t ) ).map( target=>wire( x.id, port, target ) ) ) );
	return { nodes, connections, groups: [] };
}
//the records as a graph, each node's wires a list per port it has, or is wired from
export function toGraph( data:{ nodes:CanvasNode[]; connections:CanvasWire[] } ):FlowGraph{
	const wires = new Map<string,string[][]>( data.nodes.map( n=>[n.id, Array.from( {length: flowOutputs( n )}, ():string[]=>[] )] ) );
	for( const connection of data.connections ){
		const {node, port} = outPort( connection.sourceId ), ports = wires.get( node );
		if( !ports )
			continue;
		while( ports.length<=port )
			ports.push( [] );
		ports[port].push( inNode( connection.targetId ) );
	}
	return { nodes: data.nodes.map( x=>({ id: x.id, type: x.type, name: x.name, x: x.position.x, y: x.position.y, props: x.props, wires: wires.get( x.id )! }) ) };
}