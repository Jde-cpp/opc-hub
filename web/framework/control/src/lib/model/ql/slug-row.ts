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

	static idArray( from:ISlugRow[] ):number[]{
		const clone:number[] = [];
		for( let item of from ?? [] )
			clone.push( item.id );
		return clone;
	}

	override equals( row:Partial<ISlugRow> ):boolean{
		return this.slug==row.slug && this.name==row.name && this.description==row.description;
	}

	protected addRemoveMutations<T extends SlugRow<T>>( parentType:string, originalChildren:SlugRow<T>[], modifiedChildren:SlugRow<T>[], input:any ):Mutation[]{
		let y = new Array<Mutation>();
		let getMutations = ( changes:SlugRow<T>[], type:MutationType )=>{
			for( let change of changes )
				y.push( new Mutation(parentType, change.id, input, type) );
		}
		getMutations( SlugRow.notSubset(originalChildren, modifiedChildren), MutationType.Remove );
		getMutations( SlugRow.notSubset(modifiedChildren, originalChildren), MutationType.Add );

		return y;
	}

	protected childMutations( parent:ISlugRow, originalChildren:ISlugRow[]|undefined, modifiedChildren:ISlugRow[]|undefined, input:any={} ):Mutation[]{
		let y = new Array<Mutation>();
		let addMutations = ( changes:number[], type:MutationType )=>{
			if( !changes.length )
				return;
			let mutationInput = clone( input );
			let keys = Object.keys(input);
			let inputObj = keys.length ? mutationInput[Object.keys(mutationInput)[0]] : mutationInput; // ? role:id : id:
			inputObj.id = changes;
			y.push( new Mutation(parent.type, parent.id, mutationInput, type) );
		}
		let original = originalChildren!.map( x=>x.id );
		let current = modifiedChildren!.map( x=>x.id );
		addMutations( original.filter((x)=>!current.includes(x)), MutationType.Remove );
		addMutations( current.filter((x)=>!original.includes(x)), MutationType.Add );

		return y;
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

	static notSubset<T extends SlugRow<T>>( a:T[], b:T[] ):T[]{
		let y = [];
		for( let item of a )
			if( !b.find( x=>x.id==item.id ) )
				y.push( item );
		return y;
	}
}