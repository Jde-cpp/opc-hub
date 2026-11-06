import { CnnctnSlug } from './server-cnnctn';
import { NodeId, NodeIdJson } from './node-id';
import { ENodeClass, UaNode, Variable } from './node';
import { Reading } from './value';
import { StatusCode } from './types';

//Part 4's DataChangeFilter as the socket's Subscribe asks it per node (clients spec *Watch list*, Opc.FromClient.proto):  when the
//item reports, and the deadband a value change has to clear.  The trigger left out is the server's default, StatusValue.
export enum EDataChangeTrigger{ Status = 0, StatusValue = 1, StatusValueTimestamp = 2 }
export enum EDeadbandType{ None = 0, Absolute = 1, Percent = 2 }
export type WatchFilter = { trigger?:EDataChangeTrigger; deadbandType:EDeadbandType; deadbandValue:number };
//What a watch row's item asks its server for - each left out is the gateway's default:  500 ms, a queue of one, discard oldest,
//no filter.  The shape of the proto's MonitoringParameters, so it goes on the wire as it is.
export type WatchParameters = { samplingInterval?:number; queueSize?:number; discardOldest?:boolean; filter?:WatchFilter };
//What the ack, or a MonitoredItemModified push, says the item runs at:  the server's revised values, and the status of a modify
//it refused - the item then stands as it was, at the values given.
export type Revised = { samplingInterval:number; queueSize:number; sc:StatusCode };

//A row as the profile store keeps it (spec *Open questions* #13, per user):  the connection, the node and what it asks.  What the
//node is - its name, type, value - is read again each time the list opens.
export type WatchItemJson = { opc:CnnctnSlug; node:NodeIdJson; parameters:WatchParameters };
export type WatchListJson = { items:WatchItemJson[] };
//the profile key of a gateway's list - one list per gateway, with rows from any of its connections
export function watchProfileKey( gateway:string ):string{ return `gateways/${gateway}/watch`; }

export const samplingDefault = 500, queueDefault = 1;//the gateway's own, which an unset parameter asks for (Opc.FromClient.proto)

//A row of the watch list:  the item persisted, the node as the connection describes it - a Variable, with the data type that
//renders its value and the enumeration that names it - the reading the pushes keep current, and what the server revised the
//item to.  `error` is why the row has no node or no item:  the connection could not describe it, or the subscribe was refused.
export class WatchRow{
	constructor( opc:CnnctnSlug, node:NodeId, parameters:WatchParameters ){
		this.opc = opc;
		this.node = node;
		this.parameters = parameters;
	}
	static fromJson( json:WatchItemJson ):WatchRow{ return new WatchRow( json.opc, new NodeId( json.node ), WatchRow.parametersOf( json.parameters ) ); }
	//the persisted shape, with a deadband filter read back whole:  a persisted parameter that is not a number is left out, as a
	//row edited by hand or saved by an older build would otherwise send text to the gateway
	static parametersOf( json:any ):WatchParameters{
		const y:WatchParameters = {};
		if( typeof json?.samplingInterval=='number' ) y.samplingInterval = json.samplingInterval;
		if( typeof json?.queueSize=='number' ) y.queueSize = json.queueSize;
		if( typeof json?.discardOldest=='boolean' ) y.discardOldest = json.discardOldest;
		const f = json?.filter;
		if( f && typeof f=='object' && typeof f.deadbandType=='number' && typeof f.deadbandValue=='number' ){
			y.filter = { deadbandType: f.deadbandType, deadbandValue: f.deadbandValue };
			if( typeof f.trigger=='number' )
				y.filter.trigger = f.trigger;
		}
		return y;
	}
	toJson():WatchItemJson{ return { opc: this.opc, node: this.node.toJson(), parameters: this.parameters }; }
	get key():string{ return WatchRow.keyOf( this.opc, this.node ); }
	static keyOf( opc:CnnctnSlug, node:NodeId ):string{ return `${opc}\u0000${node.uaString()}`; }//text, as NodeId.key is a Symbol
	//the node as the connection describes it:  a Variable's data type and enumeration render the value;  any other class has none to watch
	describe( node:UaNode|undefined, path:string|null|undefined ){
		this.uaNode = node;
		this.path = path ?? undefined;
		if( node instanceof Variable ){
			if( node.value!==undefined || node.sc )//the describing read's own reading, until the first push
				this.setReading( {value: node.value, sc: node.sc, source: node.sourceTime, server: node.serverTime} );
		}
		else if( node )
			this.error = `${ENodeClass[node.nodeClass]} - only a variable has a value to watch.`;
	}
	get variable():Variable|undefined{ return this.uaNode instanceof Variable ? this.uaNode : undefined; }
	get name():string{ return this.uaNode?.name || this.node.uaString(); }
	//every reading the row takes, a push or the describing read:  the quality travels with the value (Variable.setReading)
	setReading( r:Reading ){
		this.sc = r.sc ?? 0;
		this.value = r.value;
		this.sourceTime = r.source;
		this.serverTime = r.server;
	}
	revise( r:Revised ){
		this.revised = r;
	}
	opc:CnnctnSlug;
	node:NodeId;
	parameters:WatchParameters;
	uaNode?:UaNode;
	path?:string;//the node's page path from Root, as the gateway places it - none for a node it cannot place
	value?:unknown;
	sc?:StatusCode;//the reading's quality;  undefined = no reading yet
	sourceTime?:Date;
	serverTime?:Date;
	revised?:Revised;//none until the ack
	error?:string;
}

//the parameters as a cell reads them
export function samplingText( p:WatchParameters ):string{ return p.samplingInterval==undefined ? `${samplingDefault}` : `${p.samplingInterval}`; }
export function queueText( p:WatchParameters ):string{ return p.queueSize==undefined ? `${queueDefault}` : `${p.queueSize}`; }
export function discardText( p:WatchParameters ):string{ return p.discardOldest==undefined || p.discardOldest ? 'oldest' : 'newest'; }
//"StatusValue, 2 Absolute":  the trigger, then the deadband and its type - none for no filter, the server's default
export function filterText( p:WatchParameters ):string{
	const f = p.filter;
	if( !f )
		return '';
	const trigger = f.trigger==undefined ? 'StatusValue' : EDataChangeTrigger[f.trigger];
	const deadband = f.deadbandType==EDeadbandType.None ? 'no deadband' : `${f.deadbandValue}${f.deadbandType==EDeadbandType.Percent ? '%' : ''} ${EDeadbandType[f.deadbandType].toLowerCase()}`;
	return `${trigger}, ${deadband}`;
}
//what the gateway refuses (FromClientUtils::ToParameters), said before it is sent
export function parametersProblem( p:WatchParameters ):string|undefined{
	if( p.samplingInterval!=undefined && !isFinite( p.samplingInterval ) )
		return 'The sampling interval is a number of milliseconds.';
	if( p.queueSize!=undefined && (!Number.isInteger( p.queueSize ) || p.queueSize<0) )
		return 'The queue size is a whole number.';
	const f = p.filter;
	if( f ){
		if( !isFinite( f.deadbandValue ) || f.deadbandValue<0 )
			return 'The deadband is a number of at least 0.';
		if( f.deadbandType==EDeadbandType.Percent && f.deadbandValue>100 )
			return 'A percent deadband is at most 100.';
	}
	return undefined;
}