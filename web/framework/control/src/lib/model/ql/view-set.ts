import { computed, signal } from '@angular/core';
import { Sort } from '@angular/material/sort';
import { LocalPrefs, ProfileStore } from 'jde-spa';
import type { TableSettings } from '../../services/ql-list-resolver';//type-only: ql-list-resolver imports this file
import { TableSchema } from './schema/table-schema';
import { View, ViewType, ViewClass } from './view';

export type ViewSetOptions<V extends View> = {
	type:ViewClass<V>;//ql-list-settings emits a base View:  every view is re-made as this, so a subclass keeps its query()/apply()
	key:string;//the LocalPrefs owner of the selected view's number
	viewsKey?:string;//the profile row of the index, a view's row under it as `/<number>` - `<key>/views` when unset
	profileStore:ProfileStore;
	prefs:LocalPrefs;
	settings?:TableSettings;//the route's, which toJson leaves out of a saved view
	offered?:( view:View )=>boolean;//false for a view this host does not show - a pick list's, of deleted rows.  It stays in the set, and in what is written.  Over View, not V:  on V a ViewSet<NodeView> would be no ViewSet<View>, which the toolbar takes.
};

//The views a list offers, and the rules QLList, LogDetail, NodeChildren and the watch list share for them.
//They are the user's saved views, in the user's order, or - while none is saved - the route's own, a starting set:  the first
//change saved, an edit, a rename, a move or a deletion, keeps the whole set as the user's, and deleting the last shows the route's
//again.  A view is known by its number, since names repeat:  the route's are 1 to n, and a new one takes the next above the set's
//highest.  A Show or a header sort makes at most one Adhoc twin, under its view's number;  a Save writes it over that view, and
//switching away discards it.
//The profile holds a row per view, `<viewsKey>/<number>`, and the index, the numbers in tab order, at `<viewsKey>`.  A change
//writes the rows it changed, then the index, then deletes the rows it removed, so the index never names a row that is not there;
//its writes wait for the last change's, and say what the set is by then, so two changes made together leave the two agreeing.
//Where the profile could not be read the route's views show, and every change is refused with the read's error until a read
//succeeds, which the next change tries first:  a first save writes every row, and must not write them over a set it has not seen.
//The array is only ever replaced, so whatever reads current() sees the change, and the host re-queries or, where its rows are a
//computed of current(), does nothing.  The selected view's number is kept in LocalPrefs, per browser, as it is picked.
export class ViewSet<V extends View>{
	constructor( options:ViewSetOptions<V> ){
		this.#type = options.type;
		this.#key = options.key;
		this.#viewsKey = options.viewsKey ?? `${options.key}/views`;
		this.#profileStore = options.profileStore;
		this.#prefs = options.prefs;
		this.#settings = options.settings;
		this.#offered = options.offered;
	}
	//The route's views and the profile's over them, once the profile is read - nothing shows before.  A failed read is not a
	//failed load:  the list opens on the route's views.
	async load( route:V[], schema:TableSchema ):Promise<void>{
		const read = this.#profileStore.loadRows( this.#viewsKey );
		await read.catch( ()=>{} );
		this.reset( route, schema );
		try{
			this.#take( await read );
		}
		catch( e ){
			console.warn( `The views of '${this.#key}' could not be read - the list's own are shown.`, e );
		}
	}
	//the route's views alone, on the remembered one, the profile unread:  a change reads it first
	reset( route:V[], schema:TableSchema ){
		route.forEach( (view, i)=>view.number = i+1 );
		this.#route = route;
		this.#schema = schema;
		this.#members = route;
		this.#saved = this.#known = false;
		this.#twin = this.#fallback = undefined;
		this.#selected = this.#prefs.viewNumber( this.#key );
		this.#publish();
	}

	show( view:View ){
		const twin = this.#type.copy( view );
		if( twin.isAdhoc && twin.name?.endsWith("*") )
			twin.name = twin.name.slice( 0, -1 );
		twin.type = ViewType.Adhoc;
		this.#twin = twin;
		this.#selected = twin.number;
		this.#onTwin = true;
		this.#publish();
	}
	sort( sort:Sort ){
		const view = this.#type.copy( this.current() );
		view.sort = [sort, ...view.sort.filter( s=>s.active!=sort.active )];
		this.show( view );
	}
	//false for an index that is not there
	select( index:number ):boolean{
		const selected = this.#views()[index];
		if( !selected )
			return false;
		this.#onTwin = selected===this.#twin;
		if( !this.#onTwin )
			this.#twin = undefined;
		this.#selected = selected.number;
		this.#publish();
		this.#remember();
		return true;
	}
	//Over the view of its number;  under a number that is none of the set's, a new view after the last.  It is the one shown, and
	//the twin goes.  The returned promise is the profile's writes;  the views have changed when it is returned, unless the
	//profile is still to be read.
	save( view:View ):Promise<void>{
		return this.#change( ()=>{
			const saved = this.#type.copy( view );
			saved.type = ViewType.User;
			const views = [...this.#members];
			const at = views.findIndex( v=>v.number==saved.number );
			if( at<0 ){
				saved.number = Math.max( 0, ...views.map( v=>v.number ) )+1;
				views.push( saved );
			}
			else
				views[at] = saved;
			this.#twin = undefined;
			this.#selected = saved.number;
			this.#set( views );
		} );
	}
	//the view and its twin
	rename( view:View, name:string|undefined ):Promise<void>{
		return this.#change( ()=>{
			const named = ( v:V )=>{ const y = this.#type.copy( v ); y.name = name; return y; };
			const at = this.#members.findIndex( v=>v.number==view.number );
			if( at<0 )
				return;
			const views = [...this.#members];
			views[at] = named( views[at] );
			if( this.#twin?.number==view.number )
				this.#twin = named( this.#twin );
			this.#set( views );
		} );
	}
	//to the place `to` among the set's views
	move( view:View, to:number ):Promise<void>{
		return this.#change( ()=>{
			const from = this.#members.findIndex( v=>v.number==view.number );
			to = Math.max( 0, Math.min(to, this.#members.length-1) );
			if( from<0 || from==to )
				return;
			const views = [...this.#members];
			views.splice( to, 0, ...views.splice(from, 1) );
			this.#set( views );
		} );
	}
	//The view of its number, and its twin.  The selection stays where it survived, and the last view taken shows the route's again.
	remove( view:View ):Promise<void>{
		return this.#change( ()=>{
			if( !this.#members.some( v=>v.number==view.number ) )
				return;
			if( this.#twin?.number==view.number )
				this.#twin = undefined;
			this.#set( this.#members.filter( v=>v.number!=view.number ) );
		} );
	}

	//A change to the set:  made at once, and its writes queued.  Where the profile is still to be read the read comes first, and
	//the change is made once it succeeds - unless it found the user's own views, since the change was made to the route's.
	#change( change:()=>void ):Promise<void>{
		const known = this.#known, generation = this.#generation;
		if( known )
			change();
		const turn = this.#queue.catch( ()=>{} ).then( async ()=>{
			if( !known ){
				if( !this.#known )
					await this.#read();
				if( this.#generation!=generation )
					throw new Error( "Your saved views have been read:  make the change again." );
				change();
			}
			await this.#write();
		} );
		this.#queue = turn;
		return turn;
	}
	//THROWS where the read fails or a row cannot be made a view of:  the set is then not known, and nothing is written over it
	async #read():Promise<void>{
		this.#take( await this.#profileStore.loadRows( this.#viewsKey ) );
	}
	//the profile's rows:  the views the index names that have a row, in its order.  A row the index does not name is left alone.
	#take( rows:Map<string,string> ):void{
		const index = rows.get( this.#viewsKey );
		const numbers:unknown = index ? JSON.parse( index ) : [];
		const saved:V[] = [];
		for( const number of new Set( Array.isArray(numbers) ? numbers.filter( n=>Number.isInteger(n) && n>0 ) as number[] : [] ) ){
			const json = rows.get( this.#rowKey(number) );
			if( json===undefined )
				continue;
			const view = this.#type.fromJson( JSON.parse(json), this.#schema, this.#route[0] );
			view.number = number;
			saved.push( view );
		}
		this.#known = true;
		this.#stored = new Map( saved.map( v=>[v.number, this.#json(v)] ) );
		this.#storedIndex = index ?? null;//as it is held:  one naming a row that is not there is put right by the next change
		if( !saved.length )
			return;
		++this.#generation;
		this.#members = saved;
		this.#saved = true;
		this.#twin = undefined;//the route's
		this.#selected = this.#prefs.viewNumber( this.#key );
		this.#publish();
	}
	//What the set is now against what the profile holds:  the rows that differ, then the index, then the rows no longer in it.
	//What it holds is only what was written, so a failed write is made again by the next change.
	async #write():Promise<void>{
		const rows = new Map<number,string>( this.#saved ? this.#members.map( v=>[v.number, this.#json(v)] ) : [] );
		for( const [number, json] of rows ){
			if( this.#stored.get(number)===json )
				continue;
			await this.#profileStore.save( this.#rowKey(number), json );
			this.#stored.set( number, json );
		}
		const index = this.#saved ? JSON.stringify( this.#members.map( v=>v.number ) ) : null;//none saved:  no index row
		if( index!==this.#storedIndex ){
			await this.#profileStore.save( this.#viewsKey, index );
			this.#storedIndex = index;
		}
		for( const number of [...this.#stored.keys()].filter( n=>!rows.has(n) ) ){
			await this.#profileStore.save( this.#rowKey(number), null );
			this.#stored.delete( number );
		}
	}
	#rowKey( number:number ):string{ return `${this.#viewsKey}/${number}`; }
	#json( view:V ):string{ return JSON.stringify( view.toJson(this.#settings) ); }
	//The set replaced, and so the user's - or the route's again where none is left.  A view the change left alone is the object it
	//was, so a host re-reads its rows only where the view on screen is another.
	#set( views:V[] ){
		this.#saved = views.length>0;
		this.#members = this.#saved ? views : this.#route;
		this.#publish();
		this.#remember();
	}
	//The views shown:  the set's the host offers, then the twin, on the selected one or else the first.  Where the host offers
	//none of them it is the route's default view, under no number, so a Save adds it to the set.
	#publish(){
		let views = this.#offered ? this.#members.filter( this.#offered ) : [...this.#members];
		if( !views.length && this.#route.length ){
			this.#fallback ??= this.#type.copy( this.#route[0] );
			this.#fallback.number = 0;
			views = [this.#fallback];
		}
		this.#onTwin &&= !!this.#twin;
		const index = this.#onTwin ? views.length : Math.max( views.findIndex(v=>v.number==this.#selected), 0 );
		if( this.#twin )
			views.push( this.#twin );
		this.#views.set( views );
		this.#index.set( index );
		this.#selected = views[index]?.number ?? 0;
	}
	#remember(){
		if( this.#selected )//not the fallback:  a pick list that offers none of the set leaves the list's own memory alone
			this.#prefs.setViewNumber( this.#key, this.#selected );
	}

	#views = signal<V[]>( [] );
	#index = signal<number>( 0 );
	readonly views = this.#views.asReadonly();
	readonly index = this.#index.asReadonly();
	readonly current = computed<V>( ()=>this.#views()[this.#index()] );
	//the route's default view - what a view's filters are compared with to say they narrowed the list, whichever tab is first
	get routeDefault():V|undefined{ return this.#route[0]; }
	#route:V[] = [];
	#schema!:TableSchema;
	#members:V[] = [];//the set in tab order, the views not offered among them:  the user's, or the route's while none is saved
	#saved = false;//the set is the user's
	#twin:V|undefined;
	#fallback:V|undefined;
	#selected = 0;//the number of the view shown
	#onTwin = false;
	#known = false;//the profile's rows were read
	#generation = 0;//of the set a read put in the route's place
	#stored = new Map<number,string>();//the profile's rows, as they were read and written since
	#storedIndex:string|null = null;
	#queue:Promise<void> = Promise.resolve();
	readonly #type:ViewSetOptions<V>["type"];
	readonly #key:string;
	readonly #viewsKey:string;
	readonly #profileStore:ProfileStore;
	readonly #prefs:LocalPrefs;
	readonly #settings:TableSettings|undefined;
	readonly #offered:ViewSetOptions<V>["offered"];
}