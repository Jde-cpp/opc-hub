import { NodeId, NodeIdJson } from "./node-id";
import { ETypes, Browse, ILocalizedText, Ns, toLocalizedText, EAccess, EWriteAccess, StatusCode } from "./types";
import { Reading, toReading, Value } from "./value";
import { Enum } from "./enum";
import { OpcError } from "./opc-error";
import { isBad } from "./status-code";

export enum ENodeClass{
  Unspecified = 0,
  Object = 1,
  Variable = 2,
  Method = 4,
  ObjectType = 8,
  VariableType = 16,
  ReferenceType = 32,
  DataType = 64,
  nodeClassView = 128,
}

export abstract class UaNode extends NodeId{
	constructor( json:any, parent?:UaNode ){
		super( json );
		this.browse = json.browse;
		this.description = toLocalizedText( json.description );
		this.#name = toLocalizedText( json.name ); //TODO switch to just name?
		this.parent = parent;
		this.refType = json.referenceType ? new NodeId( json.referenceType ) : undefined;
		this.typeDef = json.typeDefinition ? new ObjectType( json.typeDefinition ) : undefined;
	}
	browseFQ( defaultNS:Ns|undefined ):string{
		const browse = this.browse!;
		return browse.ns===defaultNS ? browse.name.toString() : `${browse.ns}~${browse.name}`;
	}

	get nodeId(){ return new NodeId( this ); }
	get name(){ return this.#name?.text; } #name:ILocalizedText;

	browse?:Browse;
	description:ILocalizedText;
	get displayed(){ return false; }
	get isSystem(){ return this.ns==0 && this.isNumericId && this.numericId<32750; }
	get isObject(){ return this.nodeClass == ENodeClass.Object; }
	get isVariable(){ return false; }
	abstract get nodeClass():ENodeClass;
	parent?:UaNode;
	refType?:NodeId;
	specified!:number;
	typeDef?:ObjectType;
	userWriteMask!:EWriteAccess;
	writeMask!:EWriteAccess;
}

export class ObjectType extends UaNode{
	override get nodeClass():ENodeClass{ return ENodeClass.ObjectType; }
}

export enum EObjects{
	ObjectsFolder = 85, /* Object */
	Server = 2253
}
export class OpcObject extends UaNode{
	static get rootNode(){ return new OpcObject({ ns: 0, i: EObjects.ObjectsFolder, name: {locale:"en-US", text:"root"}}); }
	override get displayed(){ return !this.isSystem || this.equals(OpcObject.rootNode); }
	override get nodeClass():ENodeClass{ return ENodeClass.Object; }
}

export class Variable extends UaNode{
	constructor( json:{browseName?:Browse, dataType?:NodeIdJson, displayName:ILocalizedText, node?:NodeIdJson, nodeClass?:number, referenceType?:NodeIdJson, typeDefinition?:NodeIdJson, value?:any, valueRank?:number, accessLevel?:EAccess, userAccessLevel?:EAccess}, parent?:UaNode )	{
		super( json, parent );
		if( json.dataType?.ns ){
			this.dataType = ETypes.None;
			this.customDataType = new NodeId( json.dataType );
		}
		else
			this.dataType = <ETypes>json.dataType?.i;
		if( json.value!==undefined )//a browse that arrives Bad is a blank cell with a status - never an OpcError for the editors to bind
			this.setReading( toReading(json.value) );
		this.valueRank = json.valueRank ?? -1;
		this.accessLevel = Variable.accessLevelOf( json["accessLevel"] );
		this.userAccessLevel = Variable.accessLevelOf( json["userAccessLevel"] );
	}
	//install-issues #35:  the gateway writes a key for every attribute it asked for, whatever came back - ReadResponse::SetJson
	//loops the request, not the results - so a server that answers the AccessLevel read with an empty value sends
	//`"userAccessLevel": null`, and one that answers with a status sends `{sc}` or `{v,sc}`.  Our own OpcServer always sends a
	//number, which is why this never showed here.  `null` is not 0:  left raw it compared as 0 and every value cell on that
	//server locked itself with nothing to say, which is the whole of #35.  Unknown is `undefined` - the same thing
	//NodeView.readDenied already means by it - so one rule covers "the server did not say" everywhere.
	private static accessLevelOf( x:any ):EAccess|undefined{
		const level = x!=null && typeof x=="object" ? x["v"] : x;//{v,sc}: a reading with a status rides along with it
		return typeof level=="number" ? level as EAccess : undefined;
	}
	override get nodeClass():ENodeClass{ return ENodeClass.Variable; }

	accessLevel?:EAccess;
	userAccessLevel?:EAccess;
	dataType?:ETypes;
	customDataType?:NodeId|Enum;
	override get displayed(){ return true; }
	get isArray():boolean{ return this.valueRank!=-1 && Array.isArray(this.value); }
	override get isVariable(){ return true; }
	get isInteger():boolean{ return [ETypes.SByte, ETypes.Int16, ETypes.Int32, ETypes.Int64].includes(this.dataType!); }
	get isFloating():boolean{ return [ETypes.Float, ETypes.Double].includes(this.dataType!); }
	get isUnsigned():boolean{ return [ETypes.Byte, ETypes.UInt16, ETypes.UInt32, ETypes.UInt64].includes(this.dataType!); }
	//The one way a reading reaches a row - the browse, a subscription push, a write's echo and a re-read all come through
	//here, so the quality can't be dropped by one of them again.
	setReading( r:Reading ){
		this.sc = r.sc ?? 0;
		if( this.sc )
			OpcError.statusCodeText( this.sc );//asks for the name:  an Uncertain {v,sc} never built an OpcError, so its name was never fetched
		const usable = r.value instanceof OpcError ? undefined : r.value;//a failure is not a value:  bound into <input type=number> it rendered NaN
		if( usable!==undefined || !isBad(this.sc) )//Bad with nothing usable keeps the last value the row had
			this.value = usable;
	}
	get stale():boolean{ return isBad( this.sc ); }//`value` is the last one known, not a current reading:  shown dimmed and locked
	value?:Value;
	sc?:StatusCode;//the reading's quality (OPC 10000-4 7.38).  0 = Good;  undefined = no reading yet, which is not the same thing.
	valueRank?:number; // -1 scalar, 1 one-dimensional array, etc.
}