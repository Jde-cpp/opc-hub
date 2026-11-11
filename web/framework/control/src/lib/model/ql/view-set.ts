import { computed, signal } from '@angular/core';
import { Sort } from '@angular/material/sort';
import { LocalPrefs, ProfileStore } from 'jde-spa';
import type { TableSettings } from '../../services/ql-list-resolver';//type-only: ql-list-resolver imports this file
import { TableSchema } from './schema/table-schema';
import { View, ViewSerializedArgs, ViewType, ViewClass } from './view';

export type ViewSetOptions<V extends View> = {
	type:ViewClass<V>;//every view is re-made as this, so a subclass keeps its query()/apply() whatever class it is handed
	key:string;//the LocalPrefs owner of the selected view's number and, under the user's key, of the drafts
	viewsKey?:string;//the profile row of the index, a view's row under it as `/<number>` - `<key>/views` when unset
	profileStore:ProfileStore;
	prefs:LocalPrefs;
	settings?:TableSettings;//the route's, which toJson leaves out of a saved view
	offered?:( view:View )=>boolean;//false for a view this host does not show - a pick list's, of deleted rows.  It stays in the set, and in what is written.  Asked of a view as saved:  its draft shows wherever it does.  Over View, not V:  on V a ViewSet<NodeView> would be no ViewSet<View>, which the toolbar takes.
};

//The views a list offers, and the rules QLList, LogDetail, NodeChildren and the watch list share for them.
//They are the user's saved views, in the user's order, or - while none is saved - the route's own, a starting set:  the first
//change saved, an edit, a rename, a move or a deletion, keeps the whole set as the user's, and deleting the last shows the route's
//again.  A view is known by its number, since names repeat:  the route's are 1 to n, and a new one takes the next above the set's
//highest.
//A control changes a view by replacing it with its draft, an Adhoc twin under its number that takes the view's place among the
//views:  one a view at most, kept when its tab is left, written over its view by a Save and dropped by a Discard, or by a change
//that leaves it showing what its view does.  The drafts are the
//browser's:  a draft changed is written to LocalPrefs under the user's key and the set's, by number and without the name, which is
//the view's, the entry's other numbers left as another tab or a pick list wrote them;  a Save or a Delete takes the draft out of it
//once the profile holds the change, so a failed write leaves it for a reload.  A read of the profile puts back those whose view is
//still there and that show something else.  LocalPrefs names no user, and on a shared station one user's draft must not open on
//another's view of its number.  Signed out, or the profile unread, a draft is kept here alone.  The drafts are the user's they
//were made for:  another user's are off screen from the moment that user signs in, and go with the next change.
//The profile holds a row per view, `<viewsKey>/<number>`, and the index, the numbers in tab order, at `<viewsKey>`.  A change
//writes the rows it changed, then the index, then deletes the rows it removed, so the index never names a row that is not there;
//its writes wait for the last change's, and say what the set is by then, so two changes made together leave the two agreeing.
//Where the profile could not be read the route's views show, and every change is refused with the read's error until a read
//succeeds, which the next change tries first:  a first save writes every row, and must not write them over a set it has not seen.
//So too for a read made for another:  someone signed in since, the next change reads for them first.  Signed out, a change is made
//here alone, and the next one signed in writes it too.
//The array is only ever replaced, so whatever reads current() sees the change, and the host re-queries or, where its rows are a
//computed of current(), does nothing.  A change takes `shown`, called where it puts another view on screen:  at once, or - the
//profile still to be read - once the read lands, so a tab picked in the meantime is not taken for it.  The selected view's number
//is kept in LocalPrefs, per browser, as it is picked.
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
		this.#fallback = undefined;
		this.#drafts.clear();
		this.#draftsFor.set( this.#profileStore.userKey );
		this.#selected = this.#prefs.viewNumber( this.#key );
		this.#publish();
	}

	//the view's draft:  `view` in its place, under its view's name.  One that shows what its view does is no draft.
	show( view:View ){
		this.#claim();
		const twin = this.#type.copy( view );
		twin.type = ViewType.Adhoc;
		const of = this.#members.find( v=>v.number==twin.number ) ?? ( twin.number ? undefined : this.#fallback );
		if( of )
			twin.name = of.name;
		if( of && View.alike(twin, of) )
			this.#drafts.delete( twin.number );
		else
			this.#drafts.set( twin.number, twin );
		this.#selected = twin.number;
		this.#publish();
		this.#store( [twin.number] );
	}
	//the view shown, changed on its draft
	edit( change:( draft:V )=>void ){
		const view = this.#type.copy( this.current() );
		change( view );
		this.show( view );
	}
	sort( sort:Sort ){
		this.edit( view=>view.sortFirst(sort) );
	}
	//the view as it was saved, its draft dropped.  false where it has none.
	discard( view:View ):boolean{
		this.#claim();
		if( !this.#drafts.delete(view.number) )
			return false;
		this.#publish();
		this.#store( [view.number] );
		return true;
	}
	//false for an index that is not there
	select( index:number ):boolean{
		const selected = this.views()[index];
		if( !selected )
			return false;
		this.#selected = selected.number;
		this.#publish();
		this.#remember();
		return true;
	}
	//The toolbar's Save:  the view's draft over it, as the draft stands once the change is made - the profile may be read first, and
	//the draft changed meanwhile.  Nothing where it has none by then.  A pick list's fallback, under no number, is added after the
	//last under the next.  The returned promise is the profile's writes;  the views have changed when it is returned, unless the
	//profile is still to be read.
	saveDraft( view:View, shown?:()=>void ):Promise<void>{
		return this.#change( shown, ()=>{
			const draft = this.#drafts.get( view.number );
			if( !draft )
				return;
			const saved = this.#type.copy( draft );
			saved.type = ViewType.User;
			this.#drafts.delete( view.number );
			const views = [...this.#members];
			const at = views.findIndex( v=>v.number==saved.number );
			if( at<0 ){
				saved.number = this.#next;
				views.push( saved );
			}
			else
				views[at] = saved;
			this.#selected = saved.number;
			this.#set( views );
		}, ()=>this.#store( [view.number] ) );
	}
	//A copy of the view of its number as the user's, under the next number and its own name, after it and shown:  the menu's
	//Duplicate.  The view as saved, not its draft, which stays its view's.
	duplicate( view:View, shown?:()=>void ):Promise<void>{
		return this.#change( shown, ()=>{
			const at = this.#members.findIndex( v=>v.number==view.number );
			this.#insert( this.#type.copy(this.#members[at] ?? view), at<0 ? this.#members.length : at+1 );
		} );
	}
	//`+ New view`:  a copy of the view shown, its draft's changes included, named for its number and after the last.  The draft
	//stays its view's.
	add( shown?:()=>void ):Promise<void>{
		return this.#change( shown, ()=>{
			const copy = this.#type.copy( this.current() );
			copy.name = `View ${this.#next}`;
			this.#insert( copy, this.#members.length );
		} );
	}
	//Delete has a view to take:  one of the set, its draft with it, and not the one view of a set that is still the route's, which
	//would come straight back.
	canRemove( view:View ):boolean{
		return this.#members.some( v=>v.number==view.number ) && ( this.#saved || this.#members.length>1 );
	}
	//the view and its draft, which has no name of its own
	rename( view:View, name:string|undefined, shown?:()=>void ):Promise<void>{
		return this.#change( shown, ()=>{
			const named = ( v:V )=>{ const y = this.#type.copy( v ); y.name = name; return y; };
			const at = this.#members.findIndex( v=>v.number==view.number );
			if( at<0 )
				return;
			const views = [...this.#members];
			views[at] = named( views[at] );
			const draft = this.#drafts.get( view.number );
			if( draft )
				this.#drafts.set( view.number, named(draft) );
			this.#set( views );
		} );
	}
	//to the place `to` among the set's views
	move( view:View, to:number, shown?:()=>void ):Promise<void>{
		return this.#change( shown, ()=>{
			const from = this.#members.findIndex( v=>v.number==view.number );
			to = Math.max( 0, Math.min(to, this.#members.length-1) );
			if( from<0 || from==to )
				return;
			const views = [...this.#members];
			views.splice( to, 0, ...views.splice(from, 1) );
			this.#set( views );
		} );
	}
	//The view of its number, and its draft, at once:  the next new view can take the number.  The selection stays where it survived,
	//and the last view taken shows the route's again.
	remove( view:View, shown?:()=>void ):Promise<void>{
		return this.#change( shown, ()=>{
			if( !this.#members.some( v=>v.number==view.number ) )
				return;
			this.#drafts.delete( view.number );
			this.#set( this.#members.filter( v=>v.number!=view.number ) );
		}, ()=>this.#store( [view.number] ) );
	}

	//A change to the set:  made at once, and its writes queued, `landed` called once they are done.  Where the profile is still to
	//be read the read comes first, and the change is made once it succeeds - unless it found the user's own views, since the change
	//was made to the route's, which puts those on screen.
	#change( shown:( ()=>void )|undefined, change:()=>void, landed?:()=>void ):Promise<void>{
		const known = this.#isRead, generation = this.#generation;
		const make = ()=>{
			const before = this.current();
			change();
			if( this.current()!==before )
				shown?.();
		};
		if( known )
			make();
		const turn = this.#queue.catch( ()=>{} ).then( async ()=>{
			if( !known ){
				if( !this.#isRead )
					await this.#read();
				if( this.#generation!=generation ){
					shown?.();
					throw new Error( "Your saved views have been read:  make the change again." );
				}
				make();
			}
			await this.#write();
			landed?.();
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
		const reader = this.#profileStore.userKey, replaced = saved.length>0 || this.#saved, owner = this.#draftsFor();
		if( replaced || ( owner!==undefined && owner!=reader ) )//of the set replaced, or another user's.  One made signed out, or before the read, is kept.
			this.#drafts.clear();
		this.#draftsFor.set( reader );
		this.#known = true;
		this.#reader = reader;
		this.#written = new Set( saved.map( v=>v.number ) );
		if( replaced ){
			++this.#generation;
			this.#saved = saved.length>0;
			this.#members = this.#saved ? saved : this.#route;//none saved, over a set that was:  the last user's, not this one's
			this.#selected = this.#prefs.viewNumber( this.#key );
		}
		this.#restoreDrafts();
		this.#publish();
	}
	//The drafts are the user's they were made for, or nobody's, made signed out, which whoever signs in takes.  Another user's go
	//before a draft is changed:  `views` has kept them off screen since that user signed in.
	#claim():void{
		const user = this.#profileStore.userKey, owner = this.#draftsFor();
		if( owner!==undefined && owner!=user && this.#drafts.size ){
			this.#drafts.clear();
			this.#publish();
		}
		this.#draftsFor.set( user );
	}
	//the browser's entry of the drafts:  none signed out, or while the profile is unread, when a draft's number may be another set's
	get #draftsKey():string|undefined{
		const user = this.#reader;
		return this.#known && user && user==this.#profileStore.userKey ? `${user}/${this.#key}` : undefined;
	}
	//The browser's drafts of the set's views, under those made here, which are written to it.  One whose view is gone, that can no
	//longer be made a view of, or that shows what its view does, is dropped, here and there.  It is written only where that changed it.
	#restoreDrafts():void{
		const owner = this.#draftsKey;
		if( !owner )
			return;
		const stored = this.#prefs.get<Record<string,unknown>>( owner, ViewSet.#draftsName, {} ) ?? {};//a JSON null, which get() returns as it is, written in by hand
		const restored = new Set<number>(), dropped:number[] = [];
		for( const [key, json] of Object.entries(stored) ){
			const number = +key;
			if( this.#drafts.has(number) )
				continue;
			const of = this.#members.find( v=>v.number==number );
			const draft = of && this.#readDraft( json, of );
			if( draft ){
				this.#drafts.set( number, draft );
				restored.add( number );
			}
			else
				dropped.push( number );
		}
		const unstored = [...this.#drafts.keys()].filter( n=>!restored.has(n) );
		if( dropped.length || unstored.some( n=>n>0 ) )
			this.#store( [...dropped, ...unstored] );
	}
	//the stored draft of `of`, undefined where it is none:  unreadable, or showing what its view does
	#readDraft( json:unknown, of:V ):V|undefined{
		try{
			const draft = this.#type.fromJson( json as ViewSerializedArgs, this.#schema, this.#route[0] );
			draft.number = of.number;
			draft.name = of.name;
			draft.type = ViewType.Adhoc;
			return View.alike( draft, of ) ? undefined : draft;
		}
		catch( e ){
			console.warn( `A draft of '${this.#key}' could not be read - dropped.`, e );
			return undefined;
		}
	}
	//The drafts of `numbers` as they are here, into the browser's entry:  the others as it holds them, which another tab, or a pick
	//list of the set's, may have written since this one read it.
	#store( numbers:number[] ):void{
		const owner = this.#draftsKey;
		if( !owner )
			return;
		const stored = { ...this.#prefs.get<Record<string,unknown>>( owner, ViewSet.#draftsName, {} ) };
		for( const number of numbers.filter( n=>n>0 ) ){//not the fallback's, which is no view's
			const draft = this.#drafts.get( number );
			if( draft )
				stored[number] = {...draft.toJson(this.#settings), name: undefined};
			else
				delete stored[number];
		}
		this.#prefs.set( owner, ViewSet.#draftsName, Object.keys(stored).length ? stored : null );
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
	#insert( copy:V, at:number ):void{
		copy.type = ViewType.User;
		copy.number = this.#next;
		const views = [...this.#members];
		views.splice( at, 0, copy );
		this.#selected = copy.number;
		this.#set( views );
	}
	get #next():number{ return Math.max( 0, ...this.#members.map( v=>v.number ) )+1; }
	//The set replaced, and so the user's - or the route's again where none is left.  A view the change left alone is the object it
	//was, so a host re-reads its rows only where the view on screen is another.
	#set( views:V[] ){
		this.#saved = views.length>0;
		this.#members = this.#saved ? views : this.#route;
		this.#publish();
		this.#remember();
	}
	//The views shown:  the set's the host offers, each under its draft, on the selected one or else the first.  A draft is not asked
	//about:  the filter box shows what it holds, and a pick list's fixed terms keep the deleted rows out whatever it filters on.  Where the host offers none of them it is the route's default view, under no number, so a Save adds it to
	//the set.
	#publish(){
		let views = this.#offered ? this.#members.filter( this.#offered ) : [...this.#members];
		if( !views.length && this.#route.length ){
			this.#fallback ??= this.#type.copy( this.#route[0] );
			this.#fallback.number = 0;
			views = [this.#fallback];
		}
		const index = Math.max( views.findIndex(v=>v.number==this.#selected), 0 );
		this.#bare.set( views );
		this.#shown.set( views.map( v=>this.#drafts.get( v.number ) ?? v ) );
		this.#index.set( index );
		this.#selected = views[index]?.number ?? 0;
	}
	#remember(){
		if( this.#selected )//not the fallback:  a pick list that offers none of the set leaves the list's own memory alone
			this.#prefs.setViewNumber( this.#key, this.#selected );
	}

	#shown = signal<V[]>( [] );//the views, each under its draft
	#bare = signal<V[]>( [] );//the views as saved:  shown while the drafts are another user's
	#index = signal<number>( 0 );
	readonly views = computed<V[]>( ()=>{
		const owner = this.#draftsFor();
		return owner===undefined || owner==this.#profileStore.userKey ? this.#shown() : this.#bare();
	} );
	readonly index = this.#index.asReadonly();
	readonly current = computed<V>( ()=>this.views()[this.#index()] );
	//the route's default view - what a view's filters are compared with to say they narrowed the list, whichever tab is first
	get routeDefault():V|undefined{ return this.#route[0]; }
	#route:V[] = [];
	#schema!:TableSchema;
	#members:V[] = [];//the set in tab order, the views not offered among them:  the user's, or the route's while none is saved
	#saved = false;//the set is the user's
	#drafts = new Map<number,V>();//by its view's number
	#draftsFor = signal<string|undefined>( undefined );//whose they are:  undefined made signed out
	static readonly #draftsName = "drafts";
	#fallback:V|undefined;
	#selected = 0;//the number of the view shown
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