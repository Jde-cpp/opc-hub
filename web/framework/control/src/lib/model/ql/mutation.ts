import { verify, getEnumName } from '../../utils/utils';
import { TypeName } from '../../services/graphql';
import { StringUtils } from '../../utils/string-utils';
import { capitalize } from 'jde-spa';

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
			for( let keyItems of this.#children.values() ){
				for( let item of keyItems )
					addRemove += item.toString() ?? '';//a child with nothing to send - an Update with no args - adds nothing, not the text "null"
			}
			return addRemove;
		};
		const hasArgs = this.args && Object.keys(this.args).length;
		const name = `${getEnumName(MutationType,this.type).toLowerCase()}${capitalize(this.typeName)}`;
		const selection = this.result?.length ? `{${this.result.join(",")}}` : "";
		//a row named by its id alone - deleteUser( id:3 ).  was Remove only, so a Delete or Purge by id came back null.
		if( !hasArgs && this.id && [MutationType.Delete, MutationType.Purge, MutationType.Remove].includes(this.type) ){
			query = `${name}( id:${this.id} )${selection}`;
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
			query = `${name}( `;
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
					return `[${value.map( item=>itemClause(item) ).join(",")}]`;//join() alone stringifies an object as [object Object] and drops the quotes off a string.
				verify( typeof value == "object" );
				return `{${argClause(value, false)}}`;
			};
			//every value goes through itemClause:  argClause kept its own copy of the rules, which had drifted - an undefined
			//arg failed the verify there while itemClause wrote it as null.
			let argClause = ( itemArgs:any, continuation:boolean=true )=>{
				let clause = "";
				for( let key in itemArgs ){
					if( continuation )
						clause+=", ";
					else
						continuation = true;
					clause += `${key}:${itemClause(itemArgs[key])}`;
				}
				return clause;
			}
			query+=argClause( args, this.id>0 );
			query += ` )${selection}`;
			if( this.type!=MutationType.Create && this.#children.size ){
				query += addRemoveQuery();
			}
		}
		else if( this.#children.size )
			query = addRemoveQuery() || null;//null when no child has anything to send, which ProtoService.mutate's verify refuses
		return query;
	}

	get id(){ return this._id; }
	#children:Map<TypeName,Mutation[]> = new Map<TypeName,Mutation[]>();
	get typeName(){ return this._typeName; }
	get type(){ return this._type ?? (this.id ? MutationType.Update : MutationType.Create); }
	get variables():any{ return {}; }
}