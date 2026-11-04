import { DateUtils, Timestamp } from "jde-framework";
import { qlTime } from "./hist";
import { NodeId, NodeIdJson } from "./node-id";
import { ETypes, StatusCode } from "./types";
import { numberValue, toValue, Value } from "./value";

//One of a method's InputArguments or OutputArguments as `method( opc, id )` answers it (MethodArguments::ToJson):  named, typed
//and described as the server declares it.  The data type reads as a Variable's does - a namespace-0 id is one of ETypes,
//another namespace's the server's own type, an enumeration or a structure.
export class Argument{
	constructor( json:{name?:string, dataType?:NodeIdJson, valueRank?:number, description?:string} ){
		this.name = json.name ?? "";
		this.description = json.description ?? "";
		this.valueRank = json.valueRank ?? -1;
		if( json.dataType?.ns ){
			this.dataType = ETypes.None;
			this.customDataType = new NodeId( json.dataType );
		}
		else
			this.dataType = <ETypes>( json.dataType?.i ?? ETypes.None );
	}
	name:string;
	description:string;
	valueRank:number;
	dataType:ETypes;
	customDataType?:NodeId;
	get isArray():boolean{ return this.valueRank>=0; }//Part 3 §8.6:  -1 a scalar, -2 any, -3 a scalar or a one-dimensional array;  0 and up an array
	//the editor the argument takes, as the Children tab's cells and the hist-edit dialog have it:  an array, or a type with no
	//editor of its own, as json text (Phase 9's editors replace this);  a type of the server's own as the number an enumeration is
	//sent as.
	get editor():ArgumentEditor{
		if( this.isArray )
			return 'json';
		if( this.dataType==ETypes.Boolean )
			return 'boolean';
		if( this.customDataType || Argument.numeric.includes( this.dataType ) )
			return 'number';
		if( this.dataType==ETypes.DateTime || this.dataType==ETypes.UtcTime )
			return 'datetime';
		return 'text';
	}
	//what the label says of the type:  the built-in's name, or the server's id
	get typeName():string{ return this.customDataType ? this.customDataType.uaString() : ETypes[this.dataType] ?? `${this.dataType}`; }
	private static readonly numeric = [ETypes.SByte, ETypes.Byte, ETypes.Int16, ETypes.UInt16, ETypes.Int32, ETypes.UInt32, ETypes.Int64, ETypes.UInt64, ETypes.Float, ETypes.Double, ETypes.Number, ETypes.Integer, ETypes.UInteger, ETypes.Enumeration, ETypes.Duration];
}
export type ArgumentEditor = 'boolean'|'number'|'datetime'|'text'|'json';

//the value an argument's editor holds as typed, or undefined where the text is no value of its type:  a number box's text as a
//write sends it (numberValue), a date as the Timestamp Value::Set reads, json text parsed for an array
export function argumentValue( a:Argument, text:string, flag:boolean ):Value|undefined{
	switch( a.editor ){
	case 'boolean': return flag;
	case 'number': return numberValue( text, a.dataType );
	case 'datetime':{ const d = DateUtils.fromLocalInput( text ); return d ? <Timestamp>qlTime( d ) : undefined; }//the {seconds,nanos} Value::Set reads, as the hist-edit dialog sends one
	case 'json':
		if( !text.trim() )
			return undefined;
		try{ return JSON.parse( text ); }
		catch{ return undefined; }
	default: return text;
	}
}

//A method's arguments and name, as the gateway reads them from the InputArguments and OutputArguments properties once per
//method and keeps them with the connection (MethodArgumentCache).
export class MethodArguments{
	constructor( json:{name?:string, inputArguments?:any[], outputArguments?:any[]} ){
		this.name = json.name ?? "";
		this.inputs = ( json.inputArguments ?? [] ).map( a=>new Argument( a ) );
		this.outputs = ( json.outputArguments ?? [] ).map( a=>new Argument( a ) );
	}
	name:string;
	inputs:Argument[];
	outputs:Argument[];
}

//what `call( opc, object, method, args ){ statusCode inputArgumentResults outputArguments }` answers (CallResult::ToJson):  the
//server's answer whatever its status - a refused call carries a status per input argument, a good one the outputs
export type CallResult = { statusCode:StatusCode; inputArgumentResults:StatusCode[]; outputArguments:Value[] };
export function toCallResult( json:any ):CallResult{
	return {
		statusCode: json?.statusCode ?? 0,
		inputArgumentResults: ( json?.inputArgumentResults ?? [] ).map( ( sc:any )=>Number( sc ) ),
		outputArguments: ( json?.outputArguments ?? [] ).map( ( v:any )=>toValue( v ) )
	};
}