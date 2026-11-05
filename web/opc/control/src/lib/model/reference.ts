import { ENodeClass } from './node';
import { NodeId, NodeIdJson } from './node-id';
import { Browse } from './types';

//A node id with the DisplayName the gateway's per-connection cache had for it - a `references` row's reference type and type
//definition (DisplayNameCache);  the id where the server gave no name.
export class NamedNodeId extends NodeId{
	constructor( json:NodeIdJson & {name?:string} ){
		super( json );
		this.name = json.name;
	}
	name?:string;
	get label():string{ return this.name || this.uaString(); }
}
export type ReferenceTarget = { id:NodeId; name:string; browse?:Browse };
//One reference of a node in either direction, as `node{ references{ direction refType target class typeDef } }` answers it
//(NodeQLAwait, clients spec *Address space*):  the target with its name and browse name, its class and its type definition.
export class Reference{
	constructor( json:any ){
		this.direction = json.direction;
		this.refType = new NamedNodeId( json.refType ?? {} );
		this.target = { id: new NodeId( json.target ?? {} ), name: json.target?.name ?? '', browse: json.target?.browse };
		this.nodeClass = json["class"] ?? ENodeClass.Unspecified;
		this.typeDef = json.typeDef ? new NamedNodeId( json.typeDef ) : undefined;
	}
	direction:'Forward'|'Inverse';
	refType:NamedNodeId;
	target:ReferenceTarget;
	nodeClass:ENodeClass;
	typeDef?:NamedNodeId;
	get isForward():boolean{ return this.direction=='Forward'; }
	get className():string{ return ENodeClass[this.nodeClass] ?? `${this.nodeClass}`; }
	get targetLabel():string{ return this.target.name || this.target.id.uaString(); }
}
export const referencesQuery = "node( opc:$opc, id:$id ){ references{ direction refType target class typeDef } }";