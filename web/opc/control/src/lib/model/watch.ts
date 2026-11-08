import { CnnctnSlug } from './server-cnnctn';
import { NodeId, NodeIdJson, NodeKey } from './node-id';
import { ENodeClass, UaNode, Variable } from './node';
import { OpcError } from './opc-error';
import { isBad } from './status-code';
import { Reading, valueString } from './value';
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
//it refused - the item then stands as it was, at the values given.  No values where a request to change them failed before the
//server answered:  the item runs at the ones held.
export type Revised = { samplingInterval?:number; queueSize?:number; sc:StatusCode };

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
		this.key = WatchRow.keyOf( opc, node );
		this.seriesId = `watch/${this.key}`;
		this.trendKey = Symbol.for( this.seriesId );
	}
	static fromJson( json:WatchItemJson ):WatchRow{ return new WatchRow( json.opc, new NodeId( json.node ), WatchRow.parametersOf( json.parameters ) ); }
	//the persisted shape, with a deadband filter read back whole:  a persisted parameter that is not a number is left out, as a
	//row edited by hand or saved by an older build would otherwise send text to the gateway - and so is one the gateway or the
	//encoder would refuse, each on its own:  one refusal fails the connection's whole Subscribe (419-watch #10)
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
		for( const key of <(keyof WatchParameters)[]>Object.keys( y ) ){
			const problem = parametersProblem( {[key]: y[key]} );
			if( problem ){
				console.warn( `A watch row's ${key} is left out:  ${problem}` );
				delete y[key];
			}
		}
		return y;
	}
	toJson():WatchItemJson{ return { opc: this.opc, node: this.node.toJson(), parameters: this.parameters }; }
	readonly key:string;//made once, as the row's connection and node never change (420 #14)
	static keyOf( opc:CnnctnSlug, node:NodeId ):string{ return `${opc}\u0000${node.uaString()}`; }//text, as NodeId.key is a Symbol
	//The row's trend series:  its own, as two connections may hold one node id (#420).  `seriesId` is the chart's id, `watch/` apart
	//from Highcharts' own `highcharts-` ids whatever the slug (420 #12);  `trendKey` what its pushes carry (HistValue.series).
	readonly seriesId:string;
	readonly trendKey:NodeKey;
	//the node as the connection describes it:  a Variable's data type and enumeration render the value;  any other class has none to watch
	describe( node:UaNode|undefined, path:string|null|undefined ){
		this.error = undefined;//described again:  why it once had no node is past (419-watch #5)
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
	//an enumeration of the server's own by the name of its value, once resolved, and a StatusCode by its code's (Variable.valueName):
	//the cell shows it, and the view filters and sorts on it (419-watch #13)
	get valueName():string|undefined{ return this.variable?.valueName( this.value ); }
	get name():string{ return this.uaNode?.name || this.node.uaString(); }
	//every reading the row takes, a push or the describing read, on Variable.setReading's rules (419-watch #6):  a refused subscribe's
	//OpcError is no value, a Bad reading without one keeps the last - shown stale - and the times are the reading's, bar one without
	//them of the value and quality the row already shows
	setReading( r:Reading ){
		const shown = this.#shown;
		this.sc = r.sc ?? 0;
		const usable = r.value instanceof OpcError ? undefined : r.value;
		if( usable!==undefined || !isBad( this.sc ) )
			this.value = usable;
		this.variable?.valueNamePending( this.value );//a StatusCode value's name asked for, as the sc's is
		if( r.source || r.server || this.#shown!=shown ){
			this.sourceTime = r.source;
			this.serverTime = r.server;
		}
	}
	get #shown():string{ return `${this.sc ?? ""}:${valueString( <any>this.value )}`; }
	revise( r:Revised ){
		this.revised = { samplingInterval: r.samplingInterval ?? this.revised?.samplingInterval, queueSize: r.queueSize ?? this.revised?.queueSize, sc: r.sc };
	}
	readonly opc:CnnctnSlug;
	readonly node:NodeId;
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
	if( p.queueSize!=undefined && (!Number.isInteger( p.queueSize ) || p.queueSize<0 || p.queueSize>0xFFFFFFFF) )//a uint32 on the wire
		return 'The queue size is a whole number.';
	const f = p.filter;
	if( f ){
		const inRange = ( x:number, last:number )=>Number.isInteger( x ) && x>=0 && x<=last;
		if( f.trigger!=undefined && !inRange( f.trigger, EDataChangeTrigger.StatusValueTimestamp ) )
			return 'The trigger is Status, StatusValue or StatusValueTimestamp.';
		if( !inRange( f.deadbandType, EDeadbandType.Percent ) )
			return 'The deadband type is None, Absolute or Percent.';
		if( !isFinite( f.deadbandValue ) || f.deadbandValue<0 )
			return 'The deadband is a number of at least 0.';
		if( f.deadbandType==EDeadbandType.Percent && f.deadbandValue>100 )
			return 'A percent deadband is at most 100.';
	}
	return undefined;
}