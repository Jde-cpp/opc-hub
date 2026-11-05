import { DateUtils, Timestamp } from "jde-framework";
import { qlTime } from "./hist";
import { Enum } from "./enum";
import { NodeId, NodeIdJson } from "./node-id";
import { ETypes, StatusCode } from "./types";
import { numberValue, toValue, Value, valueString } from "./value";

//One of a method's InputArguments or OutputArguments as `method( opc, id )` answers it (MethodArguments::ToJson):  named, typed
//and described as the server declares it.  The data type reads as a Variable's does - a namespace-0 id is one of ETypes,
//another namespace's the server's own type, which the gateway sends as the namespace-0 type it derives from (baseDataType).
export class Argument{
	constructor( json:{name?:string, dataType?:NodeIdJson, baseDataType?:NodeIdJson|null, valueRank?:number, description?:string} ){
		this.name = json.name ?? "";
		this.description = json.description ?? "";
		this.valueRank = json.valueRank ?? -1;
		if( json.dataType?.ns ){
			this.dataType = ETypes.None;
			this.customDataType = new NodeId( json.dataType );
			this.baseType = json.baseDataType ? <ETypes>( json.baseDataType.i ?? ETypes.None ) : undefined;
		}
		else
			this.dataType = <ETypes>( json.dataType?.i ?? ETypes.None );
	}
	name:string;
	description:string;
	valueRank:number;
	dataType:ETypes;
	customDataType?:NodeId;
	baseType?:ETypes;//a type of the server's own:  the namespace-0 type it derives from, which the gateway sends it as;  undefined where the server names none
	enum?:Enum;//an enumeration of the server's own:  its values, from `__type` (Gateway.methodArguments)
	get isArray():boolean{ return this.valueRank>=0; }//Part 3 §8.6:  -1 a scalar, -2 any, -3 a scalar or a one-dimensional array;  0 and up an array
	get isEnumeration():boolean{ return this.customDataType!==undefined && this.baseType==ETypes.Enumeration; }
	//the type the editor works in:  the data type, or the base of a type of the server's own
	get valueType():ETypes|undefined{ return this.customDataType ? this.baseType : this.dataType; }
	//the editor the argument takes, as the Children tab's cells and the hist-edit dialog have it:  an array, or a type with no
	//editor of its own, as json text (Phase 9's editors replace this);  an enumeration of the server's own as a choice of its values,
	//or the number it is sent as where the server would not name them;  any other type of the server's own as its base's, and as json
	//where its base is unknown.
	get editor():ArgumentEditor{
		if( this.isArray )
			return 'json';
		if( this.isEnumeration )
			return this.enum?.enumValues.length ? 'enum' : 'number';
		const t = this.valueType;
		if( t===undefined )
			return 'json';
		if( t==ETypes.Boolean )
			return 'boolean';
		if( Argument.numeric.includes( t ) )
			return 'number';
		if( t==ETypes.DateTime || t==ETypes.UtcTime )
			return 'datetime';
		return 'text';
	}
	//what the label says of the type:  the built-in's name, an enumeration's own, or the server's id with the base it goes as
	get typeName():string{
		if( !this.customDataType )
			return ETypes[this.dataType] ?? `${this.dataType}`;
		if( this.enum?.name )
			return this.enum.name;
		const base = this.baseType===undefined ? undefined : ETypes[this.baseType];
		return base ? `${this.customDataType.uaString()}, a ${base}` : this.customDataType.uaString();
	}
	//an output's value as the dialog shows it:  an enumeration's by name, and an array in brackets, so an empty one reads as one
	//rather than as nothing
	valueText( v:Value ):string{
		if( Array.isArray( v ) )
			return `[${v.map( x=>this.valueText( x ) ).join( ", " )}]`;
		const named = this.enum?.enumValues.find( e=>e.id===v );
		return named ? named.name : valueString( v );
	}
	private static readonly numeric = [ETypes.SByte, ETypes.Byte, ETypes.Int16, ETypes.UInt16, ETypes.Int32, ETypes.UInt32, ETypes.Int64, ETypes.UInt64, ETypes.Float, ETypes.Double, ETypes.Number, ETypes.Integer, ETypes.UInteger, ETypes.Enumeration, ETypes.Duration];
}
export type ArgumentEditor = 'boolean'|'number'|'enum'|'datetime'|'text'|'json';

//the value an argument's editor holds as typed, or undefined where the text is no value of its type:  a number box's text as a
//write sends it (numberValue), a date as the Timestamp Value::Set reads, json text parsed for an array
export function argumentValue( a:Argument, text:string, flag:boolean ):Value|undefined{
	switch( a.editor ){
	case 'boolean': return flag;
	case 'number': return numberValue( text, a.valueType );
	case 'enum': return text=="" ? undefined : +text;//the chosen value's id, the Int32 the gateway sends
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