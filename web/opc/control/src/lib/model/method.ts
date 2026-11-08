import { Enum } from "./enum";
import { NodeId, NodeIdJson } from "./node-id";
import { ETypes, StatusCode } from "./types";
import { toValue, Value, valueString } from "./value";

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
	//the type the editor works in (ValueEditor):  the data type, or the base of a type of the server's own - none where the server names
	//no base, and the value is typed as json
	get valueType():ETypes|undefined{ return this.customDataType ? this.baseType : this.dataType; }
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