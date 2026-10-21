import { Row } from './row';
import { Mutation, MutationType } from './mutation';
import { verify, clone } from '../../utils/utils';

export type Slug = string;
export abstract class ISlugRow extends Row{
	constructor(type:string,obj:SlugRowProps | number){
		super( type );
		if( typeof obj=="number" )
			this.id = obj;
		else{
			this.id = obj.id;
			this.slug = obj.slug;
			this.name = obj.name;
			this.created = obj.created ? new Date(obj.created) : undefined;
			this.updated = obj.updated ? new Date(obj.updated) : undefined;
			this.deleted = obj.deleted ? new Date(obj.deleted) : undefined;
			this.description = obj.description;
		}
	}

	static idArray( from:ISlugRow[] ):number[]{ return (from ?? []).map( item=>item.id ); }

	override equals( row:Partial<ISlugRow> ):boolean{
		return this.slug==row.slug && this.name==row.name && this.description==row.description;
	}

	//one Remove or Add per child, on the child:  `parentType` is the child's type, `input` names this row
	protected addRemoveMutations( parentType:string, originalChildren:ISlugRow[], modifiedChildren:ISlugRow[], input:any ):Mutation[]{
		const {added, removed} = diffIds( originalChildren, modifiedChildren );
		return [...removed.map( id=>new Mutation(parentType, id, input, MutationType.Remove) ), ...added.map( id=>new Mutation(parentType, id, input, MutationType.Add) )];
	}
	//one Remove and one Add on `parent`, each with its ids:  `input`'s first key (role:) holds them, else they are its id
	protected childMutations( parent:ISlugRow, originalChildren:ISlugRow[]|undefined, modifiedChildren:ISlugRow[]|undefined, input:any={} ):Mutation[]{
		const mutation = ( ids:number[], type:MutationType )=>{
			const mutationInput = clone( input );
			const keys = Object.keys( input );
			(keys.length ? mutationInput[keys[0]] : mutationInput).id = ids;
			return new Mutation( parent.type, parent.id, mutationInput, type );
		};
		const {added, removed} = diffIds( originalChildren, modifiedChildren );
		return [...removed.length ? [mutation( removed, MutationType.Remove )] : [], ...added.length ? [mutation( added, MutationType.Add )] : []];
	}

	get canSave():boolean{ return this.name?.length>0 && this.slug?.length>0; }
	//What is wrong with a field's value, for Properties to say under it - a row that returns one should not canSave either.
	fieldError( _field:string ):string|undefined{ return undefined; }

	readonly id:number;
	slug!:Slug;
	name!:string;
	readonly created:Date|undefined;
	readonly updated:Date|undefined;
	readonly deleted:Date|undefined;
	description:string|undefined;
}
export type SlugRowProps = { id:number; slug:Slug; name:string; created:Date; updated:Date; deleted:Date; description:string; };
//A row as a ql query returns it - the slug-row props it may carry plus whatever other columns the view asked for.  The
//resolvers hand the tables plain objects, not ISlugRow instances, which is why the model classes do not fit there.
export type QLRow = Partial<SlugRowProps> & Record<string, unknown>;

export abstract class SlugRow<T extends SlugRow<T>> extends ISlugRow{
	constructor(type:string,obj:any){
		super(type, obj);
	}

	mutationArgs( original:T ){
		let args:Partial<T> = {};
		if( this.slug!=original?.slug )
			args.slug = this.slug;
		if( this.name!=original?.name )
			args.name = this.name;
		if( this.description!=original?.description )
			args.description = this.description;
		return args;
	}
	mutation( original:T ):Mutation[]{
		verify( this.canSave );
		let args = this.mutationArgs( original );
		return Object.keys(args).length ? [new Mutation( this.type, this.id, args, original.id ? MutationType.Update : MutationType.Create )] : [];
	}
}
//The ids `modified` has that `original` does not, and the ones it dropped - each in its list's order.  No `modified` is a list
//nobody edited, not an emptied one.
export function diffIds( original:readonly {id:number}[]|undefined, modified:readonly {id:number}[]|undefined ):{added:number[], removed:number[]}{
	if( !modified )
		return { added: [], removed: [] };
	const was = (original ?? []).map( x=>x.id ), now = modified.map( x=>x.id );
	return { added: now.filter( id=>!was.includes(id) ), removed: was.filter( id=>!now.includes(id) ) };
}