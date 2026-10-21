import { computed, signal } from '@angular/core';
import { Sort } from '@angular/material/sort';
import { LocalPrefs, ProfileStore } from 'jde-spa';
import type { TableSettings } from '../../services/ql-list-resolver';//type-only: ql-list-resolver imports this file
import { TableSchema } from './schema/table-schema';
import { View, ViewType, ViewClass } from './view';

export type ViewSetOptions<V extends View> = {
	type:ViewClass<V>;//ql-list-settings emits a base View:  every view is re-made as this, so a subclass keeps its query()/apply()
	key:string;//the LocalPrefs owner of the index
	viewsKey?:string;//the profile row the User views persist under - `<key>/views` when unset
	profileStore:ProfileStore;
	prefs:LocalPrefs;
	settings?:TableSettings;//the route's, which toJson leaves out of a saved view
};

//The views a list offers - its System ones, the user's saved ones and at most one Adhoc twin - and the rules QLList, LogDetail and
//NodeChildren share for them.  A Show or a header sort makes the twin, named as its view;  a Save promotes it to User unless a
//System view has the name, and persists the User views;  switching away discards the twin, and Delete takes a saved view with it.
//The array is only ever replaced, so whatever reads current() sees the change, and the host re-queries or, where its rows are a
//computed of current(), does nothing.  The index is kept in LocalPrefs, per browser, except while an Adhoc view is current:  the twin is not saved, so
//its index would point elsewhere after a reload.
export class ViewSet<V extends View>{
	constructor( options:ViewSetOptions<V> ){
		this.#type = options.type;
		this.#key = options.key;
		this.#viewsKey = options.viewsKey ?? `${options.key}/views`;
		this.#profileStore = options.profileStore;
		this.#prefs = options.prefs;
		this.#settings = options.settings;
	}
	async load( system:V[], schema:TableSchema ):Promise<void>{
		const saved = await this.#profileStore.loadArray( this.#viewsKey, json=>this.#type.fromJson(json, schema, system[0]) );
		this.reset( [...system, ...saved], this.#prefs.viewIndex(this.#key) );
	}
	reset( views:V[], index:number=0 ){
		this.#views.set( views );
		this.#index.set( Math.max(0, Math.min(index, views.length-1)) );
	}

	show( view:View ){
		const twin = this.#type.copy( view );
		if( twin.isAdhoc && twin.name?.endsWith("*") )
			twin.name = twin.name.slice( 0, -1 );
		twin.type = ViewType.Adhoc;
		const views = [...this.#views()];
		this.#set( views, ViewSet.#upsert(views, twin) );
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
		const views = this.#views().filter( v=>!v.isAdhoc || v===selected );
		this.#set( views, views.indexOf(selected) );
		return true;
	}
	//the returned promise is the profile write;  the views have changed when it is returned
	save( view:View ):Promise<void>{
		const saved = this.#type.copy( view );
		if( !(saved.isSystem && this.#views().some(v=>v.isSystem && v.name==saved.name)) )
			saved.type = ViewType.User;
		const views = this.#views().filter( v=>!v.isAdhoc );
		this.#set( views, ViewSet.#upsert(views, saved) );
		return saved.isUser ? this.#persist( views ) : Promise.resolve();
	}
	//a saved view and its twin - never a System view of the same name.  The selection stays where it survived.
	remove( view:View ):Promise<void>{
		if( !view.isUser )
			return Promise.resolve();
		const current = this.current();
		const views = this.#views().filter( v=>!(v.name==view.name && (v.isUser || v.isAdhoc)) );
		this.#set( views, Math.max(views.indexOf(current), 0) );
		return this.#persist( views );
	}

	#set( views:V[], index:number ){
		this.#views.set( views );
		this.#index.set( index );
		if( !views[index].isAdhoc )
			this.#prefs.setViewIndex( this.#key, index );
	}
	#persist( views:V[] ):Promise<void>{
		return this.#profileStore.save( this.#viewsKey, views.filter(v=>v.isUser).map(v=>v.toJson(this.#settings)) );
	}
	static #upsert<V extends View>( views:V[], view:V ):number{
		const index = views.findIndex( v=>v.name==view.name && v.type==view.type );
		if( index==-1 )
			return views.push( view )-1;
		views[index] = view;
		return index;
	}

	#views = signal<V[]>( [] );
	#index = signal<number>( 0 );
	readonly views = this.#views.asReadonly();
	readonly index = this.#index.asReadonly();
	readonly current = computed<V>( ()=>this.#views()[this.#index()] );
	readonly systemNames = computed<string[]>( ()=>this.#views().filter( v=>v.isSystem && v.name ).map( v=>v.name! ) );
	readonly #type:ViewSetOptions<V>["type"];
	readonly #key:string;
	readonly #viewsKey:string;
	readonly #profileStore:ProfileStore;
	readonly #prefs:LocalPrefs;
	readonly #settings:TableSettings|undefined;
}