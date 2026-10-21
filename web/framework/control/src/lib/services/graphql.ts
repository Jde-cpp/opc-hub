import { InjectionToken } from '@angular/core';
import { Mutation } from '../model/ql/mutation';
import { Field, FieldKind } from '../model/ql/schema/field';
import { TableSchema } from '../model/ql/schema/table-schema';

export type TypeName = string;
export type Query = {text:string, vars:any};

export interface IGraphQL{
	ql<T>( q:Query ):Promise<T>;
	query<T>( ql: string, vars?:any ):Promise<T>;
	querySingle<T>( ql: string, vars?:any ):Promise<T>;
	schema( names:string[] ):Promise<TableSchema[]>;
	schemaWithEnums( type:string ):Promise<TableSchema>;
	mutate<T>( ql: string|Mutation|Mutation[] ):Promise<T>;

	slugQuery( schema:TableSchema, slug: string, excludedColumns:string[] ):string;
	subQueries( typeName: string, id: number ):string[];
	toCollectionName( collectionDisplay:string ):string;
}

export type EnumValue = {
	id?:number;
	name:string;
	description?:string;
	isDeprecated?:boolean;
	deprecationReason?:string;
}
export type Type = {
	name:string;
	kind:FieldKind;
	description?:string;
	fields?:Field[];
	enumValues?:EnumValue[];
}

//angular-review3 C13: a typed token in place of the string one - a typo now fails the build instead of resolving to nothing at runtime, and inject() can take it.
export const IGRAPHQL = new InjectionToken<IGraphQL>( 'IGraphQL' );