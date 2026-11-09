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
//So too for a read made for another:  someone signed in since, the next change reads for them first.  Signed out, a change is made
//here alone, and the next one signed in writes it too.
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
	//Delete has a view to take:  one of the set, and not a twin, whose Delete would take the view under it where the user may mean
	//only to drop the edits;  nor the one view of a set that is still the route's, which would come straight back.
	canRemove( view:View ):boolean{
		return !view.isAdhoc && this.#members.some( v=>v.number==view.number ) && ( this.#saved || this.#members.length>1 );
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
		const known = this.#isRead, generation = this.#generation;
		if( known )
			change();
		const turn = this.#queue.catch( ()=>{} ).then( async ()=>{
			if( !known ){
				if( !this.#isRead )
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
	//Read for whoever is signed in.  Signed out there is nobody to read for, and a read would answer no rows.
	get #isRead():boolean{
		const user = this.#profileStore.userKey;
		return this.#known && ( !user || user==this.#reader );
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
		this.#reader = this.#profileStore.userKey;
		this.#written = new Set( saved.map( v=>v.number ) );
		if( !saved.length && !this.#saved )
			return;
		++this.#generation;
		this.#saved = saved.length>0;
		this.#members = this.#saved ? saved : this.#route;//none saved, over a set that was:  the last user's, not this one's
		this.#twin = undefined;//of the set replaced
		this.#selected = this.#prefs.viewNumber( this.#key );
		this.#publish();
	}
	//The set as this turn starts:  its rows, then the index, then the rows no longer in it, ProfileStore skipping what it already
	//holds.  A later change's view is named only by the index of its own turn, which has written its row.  A failed write leaves the
	//store not knowing what the server holds, so the next change makes it again.
	async #write():Promise<void>{
		if( this.#profileStore.userKey!=this.#reader )//signed out since the read
			return;
		const rows = new Map<number,string>( this.#saved ? this.#members.map( v=>[v.number, this.#json(v)] ) : [] );
		for( const number of rows.keys() )
			this.#written.add( number );//before the write:  one that failed may have landed
		await Promise.all( [...rows].map( ([number, json])=>this.#profileStore.save(this.#rowKey(number), json) ) );
		await this.#profileStore.save( this.#viewsKey, rows.size ? JSON.stringify( [...rows.keys()] ) : null );//none saved:  no index row
		await Promise.all( [...this.#written].filter( n=>!rows.has(n) ).map( async number=>{
			await this.#profileStore.save( this.#rowKey(number), null );
			this.#written.delete( number );
		} ) );
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
	#reader:string|undefined;//for whom:  undefined signed out
	#generation = 0;//of the set a read put in the route's place
	#written = new Set<number>();//the numbers whose rows the profile may hold, which a removal deletes.  A row the read found no index entry for is not among them.
	#queue:Promise<void> = Promise.resolve();
	readonly #type:ViewSetOptions<V>["type"];
	readonly #key:string;
	readonly #viewsKey:string;
	readonly #profileStore:ProfileStore;
	readonly #prefs:LocalPrefs;
	readonly #settings:TableSettings|undefined;
	readonly #offered:ViewSetOptions<V>["offered"];
}