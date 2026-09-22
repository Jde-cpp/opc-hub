import { Directive, effect, inject, model, OnDestroy, OnInit, signal } from '@angular/core';
import { ActivatedRoute, Router } from '@angular/router';
import { MatDialog } from '@angular/material/dialog';
import { ComponentPageTitle, ProfileStore, RouteItem } from 'jde-spa';
import { SnackbarService } from '../shared/snackbar/snackbar-service';
import { DetailResolverData } from '../services/detail-resolver';
import { IGraphQL } from '../services/graphql';
import { SlugRow } from '../model/ql/slug-row';
import { confirm } from '../shared/confirm/confirm-dialog';

//The skeleton user-detail, role-detail, group-detail and client-detail each carried a copy of (review3 C2).  The copies
//had already drifted twice: L2's `$new` tab-index clamp existed in ONE of the four, and each page had reinvented the
//dirty-check effect, the route.data subscription and the save/cancel pair.  What is left in a subclass is what actually
//differs - the child collections it owns, and the row it builds to save.
//@Directive() rather than a bare class: Angular refuses to inherit `model()`/`input()` from an undecorated base.
@Directive()
export abstract class DetailPage<T extends SlugRow<T> & {properties:Partial<T>}> implements OnInit, OnDestroy{
	constructor( private readonly profileKey:string ){
		this.tabIndex.set( ProfileStore.tabIndex(profileKey) );
		effect( ()=>{
			const edited = this.properties();
			if( !edited )
				return;
			if( !edited.canSave )
				this.isChanged.set( false );
			else if( !(<T>edited).equals(<any>this.row.properties) )//`any`: ServerCnnctn.equals is declared over ISlugRow, the others over Partial<T>
				this.isChanged.set( true );
		});
	}

	//Subscribed HERE, not in the constructor:  a subclass's own fields - its SelectionModels - initialise only after
	//super() returns, and route.data emits synchronously, so onRow() would have run against fields that did not exist yet.
	ngOnInit(){
		this.route.data.subscribe( (data)=>this.#load(data) );
	}
	ngOnDestroy(){ ProfileStore.setTabIndex( this.profileKey, this.tabIndex() ); }
	onTabIndexChanged( index:number ){ this.tabIndex.set( index ); }

	#load( data:any ):void{
		this.pageData = data["pageData"];
		this.row = new (this.ctor)( this.pageData.row );
		if( this.onlyPropertiesTab && this.tabIndex()>0 )
			this.tabIndex.set( 0 );//review3 L2: every tab past Properties sits behind an @if a new record fails, and mat-tab-group hard-loops on an index it cannot resolve
		this.pageData.row = null;
		this.properties.set( this.row.properties );
		this.sideNav.set( this.pageData.routing );
		this.componentPageTitle.title = this.title;
		this.onRow();
		this.isChanged.set( false );//a NEW row, and the router reuses this page across ':slug' - an edit abandoned on the
		//previous one left Save live over values nobody had touched.  Safe here: the dirty-check effects only run after
		//#load returns, and they raise the flag again if the freshly loaded row really does differ from what is on screen.
		this.isLoading.set( false );
	}

	async onSubmitClick(){
		try{
			await this.ql.mutate( this.upsert().mutation(this.row), (m)=>console.log(m) );
			this.afterMutate();
			this.router.navigate( ['..'], {relativeTo: this.route} );
		}
		catch( e ){
			this.snackbar.exception( "Save failed.", e );
		}
	}
	onCancelClick(){ this.router.navigate( ['..'], {relativeTo: this.route} ); }

	//The soft delete every page under a ql collection gets for free:  the generic `delete<Type>`/`restore<Type>` mutations
	//(Introspection.cpp advertises them per table), which stamp/clear `deleted` rather than removing the row.  Up here for
	//the same reason as the save/cancel pair - user-detail and client-detail each carried a copy of it, and the pages that
	//had no copy had no Delete at all.  A page opts in by putting the button in its template.
	async onDeleteClick(){
		const restore = this.isDeleted;
		try{
			await this.ql.mutate( `${restore ? "restore" : "delete"}${this.row.type}(id:${this.row.id})`, (m)=>console.log(m) );
			this.afterMutate();
			this.router.navigate( ['..'], { relativeTo: this.route } );
		}catch( e ){
			this.snackbar.exception( `${restore ? "Restore" : "Delete"} failed.`, e );
		}
	}
	get isDeleted():boolean{ return this.row?.deleted!=null; }//only populated when show-deleted is on - the query drops the column otherwise
	get isNew():boolean{ return !this.row?.id; }//nothing to delete until the row has been saved

	//Purge is the hard delete `delete` is not:  the row is gone, with no `deleted` stamp to restore from.  Offered only on a
	//row that is ALREADY soft-deleted - reaching it means passing through Delete and the show-deleted view first, so nothing
	//live is one click from destruction - and behind a confirmation naming the row.  A page opts in via its template.
	async onPurgeClick(){
		const name = this.row.name ?? this.row.slug;
		if( !await confirm(this.dialog, {title: `Purge ${name}?`, message: `${name} will be permanently removed. This cannot be undone - a purged row has no Restore.`, confirm: "Purge", destructive: true}) )
			return;
		try{
			await this.ql.mutate( `purge${this.row.type}(id:${this.row.id})`, (m)=>console.log(m) );
			this.afterMutate();
			this.router.navigate( ['..'], { relativeTo: this.route } );
		}catch( e ){
			this.snackbar.exception( "Purge failed.", e );
		}
	}

	protected abstract get ctor():new (item:any)=>T;//a constructor cannot be reached through T, and a subclass field would initialise too late for #load
	protected abstract upsert():T;//the edited row to save - the one thing every page assembles differently
	protected abstract onRow():void;//per-page state off the freshly loaded `row`
	protected afterMutate():void{}//a save, delete, restore or purge landed - for a page whose row something else caches (ClientDetail, reviews/m3-closing.md #5)
	protected get onlyPropertiesTab():boolean{ return !this.row.id; }//client-detail gates its extra tab on `server`, not on the id
	protected get title():string{ return this.row.name; }

	row!:T;
	pageData!:DetailResolverData<T>;
	get schema(){ return this.pageData.schema; }
	get id(){ return this.row.id; }
	isChanged = signal<boolean>( false );
	isLoading = signal<boolean>( true );
	properties = signal<Partial<T>>( null as any );
	sideNav = model.required<RouteItem>();
	tabIndex = signal<number>( 0 );
	abstract ql:IGraphQL;

	protected route = inject( ActivatedRoute );
	protected router = inject( Router );
	protected componentPageTitle = inject( ComponentPageTitle );
	protected snackbar = inject( SnackbarService );
	protected dialog = inject( MatDialog );
}
