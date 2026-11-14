import type { IFStateConnection, IFStateData, IFStateNode } from '@foblex/flow';
import { FlowGraph, FlowNode } from '../../../../model/flow';
import { flowOutputs } from '../../../../model/flow-nodes';
import { FlowNodeCounts, FlowNodeStatus, flowWireKey } from '../../../../model/flow-trace';

//The graph (docs/flows/spec.md *Flows*) against the canvas library's records (#446):  a node's wires are a list of target ids
//per output port, where the library joins two connector ids with a record of its own.  The port goes ahead of the node in a
//connector's id, since a node's id is the graph's and may hold anything.
export interface CanvasNode extends IFStateNode{ type:string; name:string; props:Record<string,unknown>; }
//`link`:  a `link out`'s edge to the `link in` of its name, which the graph holds no wire for and the view draws dashed (#456)
export type CanvasWire = IFStateConnection & { link?:boolean };

export const inId = ( node:string )=>`in:${node}`;
export const outId = ( node:string, port:number )=>`out:${port}:${node}`;
export const wireId = ( source:string, target:string )=>`${source}>${target}`;
export function outPort( connector:string ):{ node:string; port:number }{
	const at = connector.indexOf( ':', 4 );
	return { port: Number( connector.substring( 4, at ) ), node: connector.substring( at+1 ) };
}
export const inNode = ( connector:string )=>connector.substring( 3 );
export const wire = ( node:string, port:number, target:string ):CanvasWire=>({ id: wireId( outId( node, port ), inId( target ) ), sourceId: outId( node, port ), targetId: inId( target ) });
//a wire as the live push and the counts name one, `node:port>to`
export function wireKey( c:Pick<CanvasWire,'sourceId'|'targetId'> ):string{
	const {node, port} = outPort( c.sourceId );
	return flowWireKey( node, port, inNode( c.targetId ) );
}

export const canvasRecord = ( x:FlowNode ):CanvasNode=>({ id: x.id, position: {x: x.x, y: x.y}, type: x.type, name: x.name, props: x.props });
//`links`:  the edges from each `link out` to the `link in` nodes of its name in this graph, which the view draws (#456);  a
//cross-flow link's other end is no node here
export function toCanvas( graph:FlowGraph, links=false ):Partial<IFStateData<CanvasNode,CanvasWire>>{
	const nodes = graph.nodes.map( canvasRecord );
	const ids = new Set( graph.nodes.map( x=>x.id ) );//a wire to no node of the graph has no connector to end at
	const connections = graph.nodes.flatMap( x=>x.wires.flatMap( ( targets, port )=>targets.filter( t=>ids.has( t ) ).map( target=>wire( x.id, port, target ) ) ) );
	if( links ){
		const ins = graph.nodes.filter( n=>n.type=='link in' );
		for( const out of graph.nodes.filter( n=>n.type=='link out' ) )
			for( const to of ins.filter( n=>n.props['name']==out.props['name'] ) )
				connections.push( { ...wire( out.id, 0, to.id ), link: true } );
	}
	return { nodes, connections, groups: [] };
}
//the records as a graph, each node's wires a list per port it has, or is wired from;  a link's edge is no wire of the graph's
export function toGraph( data:{ nodes:CanvasNode[]; connections:CanvasWire[] } ):FlowGraph{
	const wires = new Map<string,string[][]>( data.nodes.map( n=>[n.id, Array.from( {length: flowOutputs( n )}, ():string[]=>[] )] ) );
	for( const connection of data.connections ){
		if( connection.link )
			continue;
		const {node, port} = outPort( connection.sourceId ), ports = wires.get( node );
		if( !ports )
			continue;
		while( ports.length<=port )
			ports.push( [] );
		ports[port].push( inNode( connection.targetId ) );
	}
	return { nodes: data.nodes.map( x=>({ id: x.id, type: x.type, name: x.name, x: x.position.x, y: x.position.y, props: x.props, wires: wires.get( x.id )! }) ) };
}

//What the view draws over the graph (#456):  on Activity, a node's status, its figures since the ack and its last error, and
//a wire's rate a second with its last message;  on History, a wire's count over the range as its label, the path of the
//message selected, `own`, its ancestors' and descendants', `kin`, and what Play fired a moment ago.
export type FlowLit = 'own'|'kin'|'fired';
export type FlowNodeDecor = { status?:FlowNodeStatus; counts?:FlowNodeCounts; error?:string; lit?:FlowLit };
export type FlowWireDecor = { rate?:number; label?:string; message?:string; lit?:FlowLit };
export type FlowCanvasDecor = { nodes:Map<string,FlowNodeDecor>; wires:Map<string,FlowWireDecor> };//wires by wireKey
export const noDecor = ():FlowCanvasDecor=>({ nodes: new Map(), wires: new Map() });
//a wire's rate as its stroke's width, 2 to 8:  a message a second a little, a thousand the most
export function wireWidth( rate:number|undefined ):number{ return rate && rate>0 ? Math.min( 8, 2+2*Math.log10( 1+rate ) ) : 2; }