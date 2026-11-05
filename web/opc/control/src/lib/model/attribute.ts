import { ENodeClass } from './node';
import { NodeId } from './node-id';
import { OpcError } from './opc-error';
import { isBad } from './status-code';
import { ETypes, StatusCode } from './types';
import { toReading, Value, valueString } from './value';

//The WriteMask's bits (OPC 10000-3 §8.60), as a node's WriteMask and UserWriteMask attributes carry them:  the Attributes tab's edit gate
//(clients spec *Attributes*, #417).
export enum EWriteMask{
	AccessLevel = 1<<0, ArrayDimensions = 1<<1, BrowseName = 1<<2, ContainsNoLoops = 1<<3, DataType = 1<<4, Description = 1<<5, DisplayName = 1<<6,
	EventNotifier = 1<<7, Executable = 1<<8, Historizing = 1<<9, InverseName = 1<<10, IsAbstract = 1<<11, MinimumSamplingInterval = 1<<12, NodeClass = 1<<13,
	NodeId = 1<<14, Symmetric = 1<<15, UserAccessLevel = 1<<16, UserExecutable = 1<<17, UserWriteMask = 1<<18, ValueRank = 1<<19, WriteMask = 1<<20,
	ValueForVariableType = 1<<21, DataTypeDefinition = 1<<22, RolePermissions = 1<<23, AccessRestrictions = 1<<24, AccessLevelEx = 1<<25
}
//the attributes a mask allows, by name
export function writeMaskNames( mask:number ):string[]{
	return Object.entries( EWriteMask ).filter( ([, bit])=>typeof bit=='number' && (mask & bit) ).map( ([name])=>name );
}

//How an attribute is edited - the editor the gateway's updateNode types a value for (NodeQLMAwait::AttributeType);  none for one it
//does not write, the Value among them, which is the Children tab's (updateVariable)
export type AttributeEditor = 'text'|'number'|'boolean';
//An attribute as the tab lists it:  the column the gateway reads it as (ReadAwait's names), its name in the standard, the classes that
//carry it (OPC 10000-3 §5), the WriteMask bit that allows a write, and its editor.
export type AttributeDef = { key:string; label:string; classes:number; bit?:EWriteMask; editor?:AttributeEditor };
const C = ENodeClass;
const every = C.Object|C.Variable|C.Method|C.ObjectType|C.VariableType|C.ReferenceType|C.DataType|C.View;
const types = C.ObjectType|C.VariableType|C.ReferenceType|C.DataType;
const variables = C.Variable|C.VariableType;
//in the standard's order:  the attributes every node has, then each class's
export const attributeDefs:readonly AttributeDef[] = [
	{ key: 'id', label: 'NodeId', classes: every },
	{ key: 'nodeClass', label: 'NodeClass', classes: every },
	{ key: 'browse', label: 'BrowseName', classes: every },
	{ key: 'name', label: 'DisplayName', classes: every, bit: EWriteMask.DisplayName, editor: 'text' },
	{ key: 'description', label: 'Description', classes: every, bit: EWriteMask.Description, editor: 'text' },
	{ key: 'writeMask', label: 'WriteMask', classes: every, bit: EWriteMask.WriteMask, editor: 'number' },
	{ key: 'userWriteMask', label: 'UserWriteMask', classes: every, bit: EWriteMask.UserWriteMask, editor: 'number' },
	{ key: 'rolePermissions', label: 'RolePermissions', classes: every },
	{ key: 'userRolePermissions', label: 'UserRolePermissions', classes: every },
	{ key: 'accessRestrictions', label: 'AccessRestrictions', classes: every, bit: EWriteMask.AccessRestrictions, editor: 'number' },
	{ key: 'eventNotifier', label: 'EventNotifier', classes: C.Object|C.View, bit: EWriteMask.EventNotifier, editor: 'number' },
	{ key: 'containsNoLoops', label: 'ContainsNoLoops', classes: C.View, bit: EWriteMask.ContainsNoLoops, editor: 'boolean' },
	{ key: 'dataValue', label: 'Value', classes: variables },
	{ key: 'dataType', label: 'DataType', classes: variables },
	{ key: 'valueRank', label: 'ValueRank', classes: variables, bit: EWriteMask.ValueRank, editor: 'number' },
	{ key: 'arrayDimensions', label: 'ArrayDimensions', classes: variables },
	{ key: 'accessLevel', label: 'AccessLevel', classes: C.Variable, bit: EWriteMask.AccessLevel, editor: 'number' },
	{ key: 'userAccessLevel', label: 'UserAccessLevel', classes: C.Variable, bit: EWriteMask.UserAccessLevel, editor: 'number' },
	{ key: 'accessLevelEx', label: 'AccessLevelEx', classes: C.Variable, bit: EWriteMask.AccessLevelEx, editor: 'number' },
	{ key: 'minimumSamplingInterval', label: 'MinimumSamplingInterval', classes: C.Variable, bit: EWriteMask.MinimumSamplingInterval, editor: 'number' },
	{ key: 'historizing', label: 'Historizing', classes: C.Variable, bit: EWriteMask.Historizing, editor: 'boolean' },
	{ key: 'executable', label: 'Executable', classes: C.Method, bit: EWriteMask.Executable, editor: 'boolean' },
	{ key: 'userExecutable', label: 'UserExecutable', classes: C.Method, bit: EWriteMask.UserExecutable, editor: 'boolean' },
	{ key: 'isAbstract', label: 'IsAbstract', classes: types, bit: EWriteMask.IsAbstract, editor: 'boolean' },
	{ key: 'symmetric', label: 'Symmetric', classes: C.ReferenceType, bit: EWriteMask.Symmetric, editor: 'boolean' },
	{ key: 'inverseName', label: 'InverseName', classes: C.ReferenceType, bit: EWriteMask.InverseName, editor: 'text' },
	{ key: 'dataTypeDefinition', label: 'DataTypeDefinition', classes: C.DataType }
];
//the attributes of a class, in order:  what the tab reads in one `node( opc, id ){ … }`
export function attributesOf( nodeClass:ENodeClass ):AttributeDef[]{ return attributeDefs.filter( d=>d.classes & nodeClass ); }

//One attribute of a node as read:  its value where the server answered it, else the status it refused it with - and, the Value, its
//status and both timestamps beside the reading (the gateway's `dataValue`).
export class Attribute{
	constructor( public def:AttributeDef, json:unknown ){ this.take( json ); }
	//a reading of the attribute - the read's, or a write's echo
	take( json:unknown ){
		const r = toReading( json );
		this.value = r.value instanceof OpcError ? undefined : r.value;
		this.sc = r.sc ?? 0;
		if( this.sc )
			OpcError.statusCodeText( this.sc );//asks for the name, so the next fetch brings it
		this.source = r.source;
		this.server = r.server;
	}
	value?:Value;
	sc:StatusCode = 0;
	source?:Date;
	server?:Date;
	get key():string{ return this.def.key; }
	get label():string{ return this.def.label; }
	get refused():boolean{ return isBad( this.sc ); }//the server would not answer it:  the status is all there is
	//the value as text:  a node id or a browse name as the standard spells them, a class or a namespace-0 data type by name, a structure as its json
	get text():string{
		const v:unknown = this.value;
		if( v==null )
			return "";
		if( this.key=='nodeClass' && typeof v=='number' )
			return ENodeClass[v] ?? `${v}`;
		if( typeof v=='object' && !Array.isArray(v) && Object.getPrototypeOf(v)===Object.prototype ){
			const o = <Record<string,unknown>>v;
			if( typeof o['name']=='string' )
				return `${o['ns'] ?? 0}:${o['name']}`;
			if( this.key=='dataType' && !o['ns'] && typeof o['i']=='number' && ETypes[o['i']] )
				return ETypes[o['i']];
			if( 'i' in o || 's' in o || 'g' in o || 'b' in o )
				return new NodeId( o ).uaString();
			return JSON.stringify( o );
		}
		return valueString( <Value>v );
	}
	//a mask's attributes by name, as the cell's tooltip
	get tooltip():string{ return (this.key=='writeMask' || this.key=='userWriteMask') && typeof this.value=='number' ? writeMaskNames( this.value ).join( ', ' ) || 'none' : ''; }
	//the edit gate:  the editor, where the node's UserWriteMask has the attribute's bit - the server's word, under this session's rights.
	//A mask the server did not give is unknown, and no editor:  the write would be refused whatever the user holds.
	editor( userWriteMask:number|undefined ):AttributeEditor|undefined{
		return this.def.editor && this.def.bit!==undefined && userWriteMask!==undefined && (userWriteMask & this.def.bit) ? this.def.editor : undefined;
	}
}