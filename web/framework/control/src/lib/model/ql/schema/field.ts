import { QLSchema } from './ql-schema';
import { uncapitalize } from 'jde-spa';
//https://graphql.org/learn/introspection/
export enum FieldKind{
	SCALAR=0,
	OBJECT=1,
	INTERFACE=2,
	UNION=3,
	ENUM=4,
	INPUT_OBJECT=5,
	LIST=6,
	NON_NULL=7
}

type OfTypeJson = { kind: string|FieldKind, name?: string };
export class OfType extends QLSchema{
	constructor( j:OfType|OfTypeJson ){
		super( j );
		this.kind = typeof j.kind==="string" ? FieldKind[j.kind as keyof typeof FieldKind] : j.kind;
	}
	kind:FieldKind;
}

type FieldTypeJson = { kind: string|FieldKind, name?: string, ofType?: OfTypeJson };
export class FieldType extends OfType{
	constructor( j:FieldType|FieldTypeJson ){
		super( j )
		this.ofType = j.ofType ? new OfType( j.ofType ) : undefined;
	}
	get underlyingKind():FieldKind{ return this.ofType?.kind ?? this.kind; }
	get underlyingName():string{ return this.ofType?.name ?? this.name; }
	get underlyingVariableName():string{ return uncapitalize( this.underlyingName ); }
	ofType:OfType|undefined;
}

export type NullableField = {name:string, ofType:OfTypeJson};
export class Field extends QLSchema{
	constructor( j:Partial<Field>|NullableField ){
		super( j )
		if( "ofType" in j )
			this.type = new FieldType( { kind: FieldKind.NON_NULL, ofType: j.ofType } );
		else
			this.type = new FieldType( j.type! );
	}
	type:FieldType;
	get underlyingKind(){ return this.type.kind; }
	get isNumber(){ return this.type.underlyingKind==FieldKind.SCALAR && ["Int", "Float", "ID", "UInt"].includes(this.type.underlyingName); }
	get isString(){ return this.type.underlyingKind==FieldKind.SCALAR && this.type.underlyingName=="String"; }
	get isBoolean(){ return this.type.underlyingKind==FieldKind.SCALAR && this.type.underlyingName=="Boolean"; }
	get isEnum(){ return this.type.underlyingKind==FieldKind.ENUM; }
	get isDateTime(){ return this.type.underlyingKind==FieldKind.SCALAR && ["DateTime"].includes(this.type.underlyingName); }
	get isNullable(){ return this.type.kind!=FieldKind.NON_NULL; }
}
