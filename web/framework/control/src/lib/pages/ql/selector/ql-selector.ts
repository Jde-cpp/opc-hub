import { Component, input, signal, model, effect, computed, inject, untracked, viewChild } from '@angular/core';
import { ActivatedRoute } from '@angular/router';
import { SelectionModel } from '@angular/cdk/collections';
import { Latest, LocalPrefs, ProfileStore } from 'jde-spa';
import { SnackbarService } from '../../../shared/snackbar/snackbar-service';
import { IGraphQL } from '../../../services/graphql';
import { CollectionItem, ListRoute, QLListData, QLListResolver } from '../../../services/ql-list-resolver';
import { MetaObject } from '../../../model/ql/schema/meta-object';
import { QLRow } from '../../../model/ql/slug-row';
import { arraysEqual } from '../../../utils/utils';
import { QLList } from '../list/ql-list';

//A collection's ql-list in selector mode:  the toolbar, saved views and settings panel of the collection's own list page,
//with the rows checked against a caller-owned selection of ids.
//
//Everything here is driven by effects rather than ngOnInit because the host OUTLIVES the row it is showing:  user-detail,
//role-detail and group-detail each keep one selector per tab, and the router reuses the page across ':slug', so a live
//selector is handed the next row's ids (and, for the self-excluding tabs, the next row's exclusions) with no re-creation.
@Component( {
	selector: 'ql-selector',
	template: `@if( listData() ){ <ql-list [listData]=listData() [ql]=ql() [selector]=true [(selections)]=rowSelections></ql-list> }`,
	host: {class:'main-content my-content'},
	//`properties`' inset (properties.ts :host), so a selector tab lines up with the Properties tab instead of sitting flush
	//against the tab body.  border-box is load-bearing: .my-content is width:100%, and content-box would push the 48px of
	//padding outside it.
	styles: `:host{ display: block; box-sizing: border-box; padding: 16px 24px 0; }`,
	imports: [QLList]
})
export class QLSelector{
	constructor(){
		//the list itself, reloaded whenever the caller's exclusions change - role-detail's `[excludedIds]=[role.id]` would
		//otherwise go on hiding the role we navigated AWAY from and offer the current one as a child of itself.
		effect( ()=>{
			const excludedIds = this.excludedIds();
			untracked( ()=>this.#load(excludedIds) );
		});
		//ids → rows.  The owner replaces `selections` wholesale when its row changes;  without this the tab kept the previous
		//row's checks - which the rows→ids effect below then wrote straight back onto the new row, silently marking it dirty.
		effect( ()=>{
			const ids = this.selections().selected;
			const data = this.listData();
			untracked( ()=>this.#syncRows(data, ids) );
		});
		//rows → ids.  The list only holds the current page of rows, so an id it does not show (a deleted identity, one past
		//the page size, one a view filter hides) can be neither checked nor unchecked and must survive untouched.  The
		//original order is kept so the owner's arraysEqual against the loaded ids reads "unchanged" until a box is toggled.
		//Only a toggle is the user's word, and a toggle never replaces the list's data array;  a re-query always does - a view
		//switch, a filter shown or removed, a sort, a refresh.  The list rebuilds its checks from its OWN previous checks, so a
		//member the previous rows hid came back shown and unchecked, was dropped here, and Save removed it (reviews/m3-closing.md
		//#2).  On a new array the owner's ids win:  re-derive the checks from them and publish nothing.  Not by making the
		//ids→rows effect track list.data() instead - both would be dirty in one flush, and whichever ran first would decide.
		effect( ()=>{
			const rows = this.rowSelections();
			const list = this.list();
			if( !rows || !list )
				return;
			const data = list.data();
			const prior = untracked( ()=>this.selections().selected );//outside the tracking scope, or the set() below re-runs this forever
			if( data!==this.#shownData ){
				this.#shownData = data;
				untracked( ()=>this.#syncRows(this.listData(), prior) );
				return;
			}
			const shown = new Set<number>( data.map( r=>r.id ) );
			const checked = rows.selected.map( r=>r.id );
			const ids = [...prior.filter( id=>!shown.has(id) || checked.includes(id) ), ...checked.filter( id=>!prior.includes(id) )];
			if( !arraysEqual(ids, prior) )
				this.selections.set( new SelectionModel<number>(true, ids) );
		});
	}

	async #load( excludedIds:number[] ):Promise<void>{
		const load = this.#latest.next();
		//dropped BEFORE the query, not after it:  ql-list reads [listData] once, in its own ngOnInit, so a RELOAD only takes
		//effect if the @if tears the old one down first - and two sets in one turn would coalesce into no change at all.
		this.listData.set( undefined );
		try{
			//the collection's columns/sort/exclusions sit on the sibling ':collectionDisplay' route, where DetailResolver finds them too
			const collections:CollectionItem[] = this.#route.snapshot.parent?.routeConfig?.children?.find( x=>x.path==":collectionDisplay" )?.data?.["collections"] ?? [];
			const collectionName = this.collectionName();
			//A deleted identity is not something to add to a group, even where the collection's own list was last left on Deleted.
			//Not offered rather than taken out of the set:  the set is the list page's too, and a first save from here writes it whole.
			let data = await QLListResolver.data( this.ql(), ListRoute.find(collectionName, collections), this.#profileStore, this.#prefs, v=>!QLListResolver.isDeletedView(v) );
			data.fixedFilter = QLSelector.fixedFilter( data, excludedIds );
			data = await QLListResolver.load( this.ql(), data, null );
			if( !this.#latest.is(load) )
				return;
			this.#syncRows( data, this.selections().selected );
			this.listData.set( data );
		}
		catch( e ){
			this.#snackbar.exception( "Could not load values", e );
		}
	}

	//A pick list's own terms, anded with the view's as a repeated field is:  the ids it leaves out, and no deleted row, so
	//`has:deleted` typed in its box lists nothing.  Not on a live toggle's list, where a deleted row is one switched off.
	static fixedFilter( data:QLListData, excludedIds:number[] ):string{
		const deleted = data.profile.routeDefault?.fields.find( f=>f.name=="deleted" );
		return [...( excludedIds.length ? [`-id:${excludedIds.join( "," )}`] : [] ), ...( deleted && !deleted.liveToggle ? ["no:deleted"] : [] )].join( " " );
	}

	//the caller's ids as the row objects the list checks against.  A no-op when they already agree, which is the usual case:
	//the rows→ids effect has just published a toggle and the owner is echoing it back, and resetting here would fight it.
	#syncRows( data:QLListData|undefined, ids:number[] ):void{
		if( !data )
			return;//nothing loaded yet - #load syncs once it has rows
		const source:QLRow[] = this.list()?.data() ?? data.results[this.collectionName()];//the list's own rows once it has re-queried (a view change builds new objects)
		const rows = source.filter( (r:QLRow)=>r.id!=undefined && ids.includes(r.id) );
		const prior = this.rowSelections()?.selected;
		if( prior && prior.length==rows.length && rows.every(r=>prior.includes(r)) )
			return;
		this.rowSelections.set( new SelectionModel<QLRow>(true, rows) );
	}

	type = input.required<string>();
	ql = input.required<IGraphQL>();
	excludedIds = input<number[]>( [] );
	selections = model.required<SelectionModel<number>>();

	collectionName = computed<string>( ()=> MetaObject.toCollectionName(this.type()) );
	listData = signal<QLListData|undefined>( undefined );
	rowSelections = signal<SelectionModel<QLRow>>( null as any );
	list = viewChild( QLList );

	#latest = new Latest();
	#shownData:QLRow[]|undefined;//the list's data array the rows→ids effect last saw - a different one is a re-query
	#route = inject( ActivatedRoute );
	#profileStore = inject( ProfileStore );
	#prefs = inject( LocalPrefs );
	#snackbar = inject( SnackbarService );
}