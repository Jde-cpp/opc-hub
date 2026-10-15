import { verify, getEnumName } from '../../utils/utils';
import { TypeName } from '../../services/graphql';
import { StringUtils } from '../../utils/string-utils';

export enum MutationType{
	Create,
	Update,
	Delete,
	Purge,
	Add,
	Remove
}
export class Mutation{
	constructor( private _typeName:TypeName, private _id:number=0, public args:any=null, private _type:MutationType, private result:string[]|undefined=undefined ){
	}
	add( child:Mutation ){
		if( !this.#children.has(child.typeName) )
			this.#children.set( child.typeName, [child] );
		else
			this.#children.get(child.typeName)!.push( child );
	}
	toString():string|null{
		let query = null;
		let addRemoveQuery = ()=>{
			let addRemove = '';
			for( let [key, keyItems] of this.#children ){
				for( let item of keyItems )
					addRemove += item.toString();
			}
			return addRemove;
		};
		const hasArgs = this.args && Object.keys(this.args).length;
		//a row named by its id alone - deleteUser( id:3 ).  was Remove only, so a Delete or Purge by id came back null.
		if( !hasArgs && this.id && [MutationType.Delete, MutationType.Purge, MutationType.Remove].includes(this.type) ){
			query = `${getEnumName(MutationType,this.type).toLowerCase()}${StringUtils.capitalize(this.typeName)}( id:${this.id} )`;
			if( this.#children.size )
				query += addRemoveQuery();
		}
		else if( hasArgs ){
			let args = this.args;
			if( this.type==MutationType.Create && this.#children.size ){
				args = JSON.parse(JSON.stringify(this.args));
				for( let [key, keyItems] of this.#children ){
					args[key] = [];
					for( let item of keyItems )
						args[key].push( item.args );
				}
			}
			query = `${getEnumName(MutationType,this.type).toLowerCase()}${StringUtils.capitalize(this.typeName)}( `;
			if( this.id )
				query += `id:${this.id}`;
			let itemClause = ( value:any ):string=>{
				if( value===null || value===undefined )
					return "null";
				if( typeof value == "string" )
					return StringUtils.qlString( value );
				if( typeof value == "number" || typeof value == "boolean" )
					return `${value}`;
				if( Array.isArray(value) )
					return `[${value.map( item=>itemClause(item) ).join(",")}]`;
				return `{${argClause(value, false)}}`;
			};
			let argClause = ( itemArgs:any, continuation:boolean=true )=>{
				let clause = "";
				for( let key in itemArgs ){
					let value = itemArgs[key];
					if( continuation )
						clause+=", ";
					else
						continuation = true;
					if( value===null )
						clause += `${key}:null`;
					else if( typeof value == "string" )
						clause += `${key}:${StringUtils.qlString(value)}`;
					else if( typeof value == "number" || typeof value=="boolean" )
						clause += `${key}:${value}`;
					else if( Array.isArray(value) )
						clause += `${key}:[${value.map( item=>itemClause(item) ).join(",")}]`;//join() alone stringifies an object as [object Object] and drops the quotes off a string.
					else{
						verify( typeof value == "object" );
						clause += `${key}:{${argClause(value, false)}}`;
					}
				}
				return clause;
			}
			query+=argClause( args, this.id>0 );
			query += ` )`;
			if( this.result?.length )
				query += `{${this.result.join(",")}}`;
			if( this.type!=MutationType.Create && this.#children.size ){
				query += addRemoveQuery();
			}
		}
		else if( this.#children.size )
			query = addRemoveQuery();
		return query;
	}

	get id(){ return this._id; }
	#children:Map<TypeName,Mutation[]> = new Map<TypeName,Mutation[]>();
	get isEmpty():boolean{ return this.type!=MutationType.Remove && !this.args && !this.#children.size; }
	get typeName(){ return this._typeName; }
	get type(){ return this._type ?? (this.id ? MutationType.Update : MutationType.Create); }
	get variables():any{ return {}; }
}