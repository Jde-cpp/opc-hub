import { MetaObject } from "./meta-object";
import { Field, FieldKind } from "./field";
import { EnumValue } from '../../../services/graphql';

export class TableSchema extends MetaObject{
	constructor( j:any ){
		super( j.name ?? j.type );
		j.fields.forEach( (x:any)=>this.fields.push(new Field(x)) );
		this.subType = j.subType;
		this.enums = j.enums;
	}
	get columns():string{
		let result = '';
		for( const column of this.nonListFields )
			result += column.type.underlyingKind==FieldKind.OBJECT ? `${column.type.underlyingVariableName}{ id name } ` : `${column.name} `;

		return result;
	}
	find( name:string ):Field{ return this.fields.find((x)=>x.name==name)!; }
	fields = new Array<Field>();
	get nonListFields():Array<Field>{ return this.fields.filter((x)=>x.type.kind!=FieldKind.LIST); }
	get listFields():Array<Field>{ return this.fields.filter((x)=>x.type.kind==FieldKind.LIST); }
	get display(){ return this.subType?.collectionDisplay ?? this.type+'s'; }
	subType:MetaObject;
	enums: Map<string, EnumValue[]>;
}