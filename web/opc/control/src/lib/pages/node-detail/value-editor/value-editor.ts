import { Component, computed, effect, input, model, signal, untracked } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatSelectModule } from '@angular/material/select';
import { MatSlideToggleModule } from '@angular/material/slide-toggle';
import { MatTooltipModule } from '@angular/material/tooltip';
import Long from 'long';
import { DateUtils, Guid, ProtoUtils, Timestamp } from 'jde-framework';
import { Enum } from '../../../model/enum';
import { ExNodeId } from '../../../model/ex-node-id';
import { NodeId } from '../../../model/node-id';
import { OpcError } from '../../../model/opc-error';
import { parseStatus, scHex } from '../../../model/status-code';
import { ETypes } from '../../../model/types';
import { bytesOfHex, ExtensionObject, hexOf, isPlain, isStructure, jsonShape, numberValue, parseGuid, Structure, Value, valueString } from '../../../model/value';

//The editor a value takes (clients spec *Values and writes*, #423):  one per built-in type, an enumeration a choice of its values, an array
//a list of its elements, a structure its members by name, each as its own value says - and a value of no known type the kind its shape
//says (inferKind).  `undecoded` is a body no type explains, which has no editor;  `json` is the fallback, text parsed as json.
export type EditorKind = 'boolean'|'number'|'enum'|'datetime'|'text'|'xml'|'guid'|'byteString'|'nodeId'|'expandedNodeId'|'statusCode'|'qualifiedName'|'localizedText'|'json'|'array'|'structure'|'undecoded';
const numbers = [ETypes.SByte, ETypes.Byte, ETypes.Int16, ETypes.UInt16, ETypes.Int32, ETypes.UInt32, ETypes.Int64, ETypes.UInt64, ETypes.Float, ETypes.Double, ETypes.Number, ETypes.Integer, ETypes.UInteger, ETypes.Enumeration, ETypes.Duration];
export function editorKind( type:ETypes|undefined, isArray:boolean, enumeration:Enum|undefined, value:Value|null|undefined ):EditorKind{
	if( isArray )
		return 'array';
	if( enumeration?.enumValues.length )
		return 'enum';
	switch( type ){
	case ETypes.Boolean: return 'boolean';
	case ETypes.String: return 'text';
	case ETypes.XmlElement: return 'xml';
	case ETypes.DateTime: case ETypes.UtcTime: return 'datetime';
	case ETypes.Guid: return 'guid';
	case ETypes.ByteString: return 'byteString';
	case ETypes.NodeId: return 'nodeId';
	case ETypes.ExpandedNodeId: return 'expandedNodeId';
	case ETypes.StatusCode: return 'statusCode';
	case ETypes.QualifiedName: return 'qualifiedName';
	case ETypes.LocalizedText: return 'localizedText';
	case ETypes.Structure: return isStructure( value ) ? 'structure' : inferKind( value );
	default:
		if( type!==undefined && numbers.includes( type ) )
			return 'number';
		//no value to go by - a type of the server's own with none yet, a new edit - is text, as the cell editors had it
		//(reviews-issue/423-clients-writes.md #7);  a null, an absent member's, stays json
		return value===undefined ? 'text' : inferKind( value );
	}
}
//the kind a value's own shape says - a type of the server's own that is no enumeration, a BaseDataType variable's, a structure's
//member:  a plain object a structure, bar the shapes REST's json gives other kinds as (jsonShape)
export function inferKind( value:Value|null|undefined ):EditorKind{
	if( typeof value=='boolean' )
		return 'boolean';
	if( typeof value=='number' || value instanceof Long )
		return 'number';
	if( typeof value=='string' )
		return 'text';
	if( value instanceof Guid )
		return 'guid';
	if( value instanceof Uint8Array )
		return 'byteString';
	if( value instanceof ExNodeId )
		return 'expandedNodeId';
	if( value instanceof NodeId )
		return 'nodeId';
	if( value instanceof OpcError )
		return 'statusCode';
	if( value instanceof ExtensionObject )
		return 'undecoded';
	if( Array.isArray( value ) )
		return 'array';
	if( isPlain( value ) ){
		switch( jsonShape( value ) ){
		case 'timestamp': return 'datetime';
		case 'long': return 'number';
		case 'qualifiedName': return 'qualifiedName';
		case 'nodeId': return 'nodeId';
		case 'expandedNodeId': return 'expandedNodeId';
		case 'extensionObject': return 'undecoded';
		case 'empty': return 'json';
		default: return 'structure';
		}
	}
	return 'json';
}
const guidHint = "8-4-4-4-12 hex digits.", hexHint = "Hex digits, two per byte.", nodeIdHint = "ns=2;i=5, ns=2;s=tag, g= or b=.", expandedHint = "svr=1;nsu=urn:x;i=5 - the server index and uri optional.", statusHint = "Hex with 0x, or decimal.";

//A value's editor (clients spec *Values and writes*, #423):  the control its type takes, the value it holds as typed - `undefined` while the
//text is no value of the type - and, nested in itself, an array as a list of its elements and a structure as its members by name.  The
//write dialog's, the hist-edit dialog's and the method dialog's, so the three agree on what a type is typed as.  A 64-bit integer goes as
//a Long built from the text, exact past 2^53;  a DateTime as the date and time typed, local, to the millisecond;  a LocalizedText as its
//text alone, which keeps the node's locale, or {locale, text} where one is named;  a ByteString as hex, the spelling a read gives.  The
//value given from outside - the row's, a preset's, another node's - is what the fields show;  one the editor itself emitted is not read back.
@Component({
	selector: 'value-editor',
	templateUrl: './value-editor.html',
	styleUrl: './value-editor.scss',
	imports: [MatButtonModule, MatFormFieldModule, MatIconModule, MatInputModule, MatSelectModule, MatSlideToggleModule, MatTooltipModule],
	host: { '[class.nested]': 'nested()' }
})
export class ValueEditor{
	constructor(){
		effect( ()=>{
			const v = this.value();
			if( v!==this.#emitted )
				untracked( ()=>this.#load( v ) );
		} );
	}
	type = input<ETypes|undefined>();//the built-in type to edit as;  none, and the value's shape decides (inferKind)
	enumeration = input<Enum|undefined>();//an enumeration of the server's own:  a choice of its values
	isArray = input( false );
	fixedLength = input( false );//an array's elements may not be added or removed:  an index range's slice
	label = input( 'Value' );
	disabled = input( false );
	nested = input( false );//an element's or a member's, inside another editor
	value = model<Value|null|undefined>();

	#seed = signal<Value|null|undefined>( undefined );//the value given from outside, whose shape picks the editor where the type doesn't
	#emitted:unknown = Symbol();//what this editor last emitted, so its own value coming back is not reloaded into the fields
	kind = computed<EditorKind>( ()=>editorKind( this.type(), this.isArray(), this.enumeration(), this.#seed() ) );
	text = signal( "" );//the scalar as typed
	text2 = signal( "" );//a LocalizedText's locale, a QualifiedName's namespace index
	flag = signal( false );
	choice = signal<number|undefined>( undefined );
	elements = signal<( Value|null|undefined )[]>( [] );
	members = signal<{name:string; value:Value|null|undefined}[]>( [] );
	//a 64-bit integer's signedness, from the type or from the Long held:  what numberValue builds a Long for
	numberType = computed<ETypes|undefined>( ()=>{
		const t = this.type();
		if( t==ETypes.Int64 || t==ETypes.UInt64 )
			return t;
		const seed = this.#seed();
		return seed instanceof Long ? ( seed.unsigned ? ETypes.UInt64 : ETypes.Int64 ) : isPlain( seed ) && jsonShape( seed )=='long' ? ( seed['unsigned'] ? ETypes.UInt64 : ETypes.Int64 ) : t;
	} );
	enumValues = computed( ()=>this.enumeration()?.enumValues ?? [] );
	//why the text is no value, beside the field;  nothing while it is one, or blank
	invalid = computed<string|undefined>( ()=>{
		if( this.value()!==undefined || !this.text().trim() && !this.text2().trim() )
			return undefined;
		switch( this.kind() ){
		case 'number': return this.numberType()==ETypes.Int64 || this.numberType()==ETypes.UInt64 ? "A whole number in the type's range." : "A number.";
		case 'datetime': return "A date and time.";
		case 'guid': return guidHint;
		case 'byteString': return hexHint;
		case 'nodeId': return nodeIdHint;
		case 'expandedNodeId': return expandedHint;
		case 'statusCode': return statusHint;
		case 'qualifiedName': return "The namespace index is 0 to 65535.";//one line under the name field (reviews-issue/423-clients-writes.md W1)
		case 'json': return "Json.";
		default: return undefined;
		}
	} );
	hint( kind:EditorKind ):string{
		switch( kind ){
		case 'guid': return guidHint;
		case 'byteString': return hexHint;
		case 'nodeId': return nodeIdHint;
		case 'expandedNodeId': return expandedHint;
		case 'statusCode': return statusHint;
		default: return "";
		}
	}
	readonly jsonPlaceholder = '[1, 2] or {"Low": 0}';//a brace in a template is a block's
	undecoded = computed<ExtensionObject|undefined>( ()=>{ const s = this.#seed(); return s instanceof ExtensionObject ? s : isPlain( s ) && jsonShape( s )=='extensionObject' ? new ExtensionObject( new NodeId( s['typeId'] ), <any>s['body'] ) : undefined; } );

	setText( text:string ){ this.text.set( text ); this.#emit(); }
	setText2( text:string ){ this.text2.set( text ); this.#emit(); }
	setFlag( flag:boolean ){ this.flag.set( flag ); this.#emit(); }
	setChoice( id:number ){ this.choice.set( id ); this.#emit(); }
	setElement( i:number, v:Value|null|undefined ){ this.elements.update( e=>e.map( (x, j)=>j==i ? v : x ) ); this.#emit(); }
	addElement(){ this.elements.update( e=>[...e, e.length ? e[e.length-1] : undefined] ); this.#emit(); }//a copy of the last, so a number list grows a number
	removeElement( i:number ){ this.elements.update( e=>e.filter( (_, j)=>j!=i ) ); this.#emit(); }
	setMember( i:number, v:Value|null|undefined ){ this.members.update( m=>m.map( (x, j)=>j==i ? {name: x.name, value: v} : x ) ); this.#emit(); }
	//the element's type:  the array's, or none for an array of no known type, where each element's own shape decides
	elementType = computed<ETypes|undefined>( ()=>this.type() );

	#emit(){
		const v = this.parse();
		this.#emitted = v;
		this.value.set( v );
	}
	//the fields as a value of the kind, or undefined where they hold none
	parse():Value|null|undefined{
		const text = this.text().trim();
		switch( this.kind() ){
		case 'boolean': return this.flag();
		case 'number': return numberValue( this.text(), this.numberType() );
		case 'enum': return this.choice();
		case 'datetime':{
			const seed = this.#seed(), from = isPlain( seed ) && jsonShape( seed )=='timestamp' ? ProtoUtils.toDate( <Timestamp>seed ) ?? undefined : undefined;
			const d = DateUtils.fromLocalInput( this.text(), from );//from the value's time, so an edit in the hour DST repeats stays in its half
			return d ? <Timestamp>ProtoUtils.fromDate( d ) : undefined;}
		case 'text': case 'xml': return this.text();
		case 'guid': return parseGuid( text );
		case 'byteString': return bytesOfHex( text );
		case 'nodeId': try{ return text ? NodeId.fromUaString( text ) : undefined; }catch{ return undefined; }
		case 'expandedNodeId': try{ return text ? ExNodeId.fromUaString( text ) : undefined; }catch{ return undefined; }
		case 'statusCode':{ const sc = parseStatus( text ); return sc===null ? undefined : sc; }
		case 'qualifiedName':{
			const ns = this.text2().trim();
			return /^\d*$/.test( ns ) && +ns<=65535 ? <Structure>{ ns: ns ? +ns : 0, name: this.text() } : undefined;}//a UInt16:  the gateway refuses one past it
		case 'localizedText':{ const locale = this.text2().trim(); return locale ? <Structure>{ locale, text: this.text() } : this.text(); }//a bare text keeps the node's locale (reviews-issue/417-clients-attributes.md)
		case 'json':
			if( !text )
				return this.#seed()===null ? null : undefined;//an absent optional member stays absent
			try{ return JSON.parse( text ); }catch{ return undefined; }
		case 'array':{ const e = this.elements(); return e.every( x=>x!==undefined ) ? <Value[]>e : undefined; }
		case 'structure':{ const m = this.members(); return m.every( x=>x.value!==undefined ) ? <Structure>Object.fromEntries( m.map( x=>[x.name, x.value] ) ) : undefined; }
		default: return this.#seed();//undecoded:  the body as it is, which a write refuses by name (valueJson)
		}
	}
	//the fields from a value given from outside, by the kind it picks.  A toggle and a text box always hold a value - false, blank - so
	//none given is one emitted, and a dialog never waits on them.
	#load( v:Value|null|undefined ){
		this.#seed.set( v );
		this.#emitted = v;
		switch( this.kind() ){
		case 'boolean':
			this.flag.set( v===true );
			if( v===undefined )
				this.#emit();
			break;
		case 'number': this.text.set( typeof v=='number' || v instanceof Long ? v.toString() : isPlain( v ) && jsonShape( v )=='long' ? new Long( <number>v['low'], <number>v['high'], <boolean>v['unsigned'] ).toString() : "" ); break;
		case 'enum': this.choice.set( typeof v=='number' ? v : undefined ); break;
		case 'datetime':{ const d = isPlain( v ) && jsonShape( v )=='timestamp' ? ProtoUtils.toDate( <Timestamp>v ) : null; this.text.set( d ? DateUtils.toLocalInput( d ) : "" ); break; }
		case 'guid': this.text.set( v instanceof Guid ? v.toString() : typeof v=='string' ? v : "" ); break;
		case 'byteString': this.text.set( v instanceof Uint8Array ? hexOf( v ) : typeof v=='string' ? v : "" ); break;
		case 'nodeId': this.text.set( v instanceof NodeId ? v.uaString() : isPlain( v ) && jsonShape( v )=='nodeId' ? new NodeId( v ).uaString() : typeof v=='string' ? v : "" ); break;
		case 'expandedNodeId': this.text.set( v instanceof ExNodeId ? v.uaString() : v instanceof NodeId ? v.uaString() : isPlain( v ) && ['nodeId', 'expandedNodeId'].includes( jsonShape( v )! ) ? new ExNodeId( <any>v ).uaString() : typeof v=='string' ? v : "" ); break;
		case 'statusCode': this.text.set( typeof v=='number' ? scHex( v ) : v instanceof OpcError ? scHex( v.sc ) : "" ); break;
		case 'qualifiedName':{//{ns, name}, REST's and a push's alike
			const q = isPlain( v ) && jsonShape( v )=='qualifiedName' ? v : undefined;
			this.text2.set( q ? `${q['ns'] ?? 0}` : "" );
			this.text.set( q ? <string>q['name'] : "" );
			break;}
		case 'localizedText':
			if( isPlain( v ) && typeof v['text']=='string' ){
				this.text.set( v['text'] );
				this.text2.set( typeof v['locale']=='string' ? v['locale'] : "" );
			}
			else{
				this.text.set( typeof v=='string' ? v : "" );
				this.text2.set( "" );
			}
			break;
		case 'json': this.text.set( v===undefined ? "" : v===null ? "" : typeof v=='string' ? v : JSON.stringify( v ) ); break;
		case 'array': this.elements.set( Array.isArray( v ) ? [...v] : [] ); break;
		case 'structure': this.members.set( isStructure( v ) ? Object.entries( v ).map( ([name, value])=>({name, value}) ) : [] ); break;
		case 'text': case 'xml':
			this.text.set( typeof v=='string' ? v : v==null ? "" : valueString( v ) );
			if( v===undefined )
				this.#emit();
			break;
		default: break;
		}
	}
}