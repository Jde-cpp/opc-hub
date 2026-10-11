import { Component, OnDestroy, OnInit, ViewChild, input, effect, Signal, signal, inject, computed, viewChild } from '@angular/core';
import { CommonModule } from '@angular/common';
import { MatTable, MatTableModule } from '@angular/material/table';
import {MatSortModule, Sort} from '@angular/material/sort';
import { LogDataSource } from '../log-data-source';
import {LogSettings} from '../log-settings';
import { QLListSettings } from '../../ql/list/ql-list-settings/ql-list-settings';
import {SnackbarService} from '../../../shared/snackbar/snackbar-service';


import { ELogLevel } from 'jde-proto/Log';
import { MatToolbar } from '@angular/material/toolbar';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIcon } from '@angular/material/icon';
import { MatIconButton } from '@angular/material/button';
import { MatButtonToggle, MatButtonToggleGroup } from '@angular/material/button-toggle';
import { MatChip } from '@angular/material/chips';
import { MatTooltip } from '@angular/material/tooltip';
import {MatSelectModule} from '@angular/material/select';
import { PageEvent, Paginator } from '../../../shared/paginator/paginator';
import { ProfileStore } from 'jde-spa';
import { IGraphQL } from '../../../services/graphql';
import { TableSchema } from '../../../model/ql/schema/table-schema';
import { verify } from '../../../utils/utils';
import { View, ViewType } from '../../../model/ql/view';
import { Entry,LogEntries, LogEntriesRest, LogView } from '../log-entry';

// Move levels to combo.
// Add dates.
// Fix pause button.
// Comment out statuses
@Component({
	selector: 'log-detail',
	//.main-content.mat-drawer-container.my-content
	templateUrl: './log-detail.html',
	styleUrls: ['./log-detail.scss'],
	imports: [CommonModule, MatButtonToggle, MatButtonToggleGroup, MatChip, MatFormFieldModule, MatIcon, MatIconButton, MatTableModule, MatToolbar, MatTooltip, MatSelectModule, MatSortModule, Paginator, QLListSettings]
})
export class LogDetail implements OnInit, OnDestroy{
	private snackBar:SnackbarService = inject( SnackbarService );

	async ngOnInit(){
		this.data = new LogDataSource( this.view );
		this.profile = new LogSettings( await this.profileStore.load<LogSettings>("logs", new LogSettings()) );
		this.data.filter.level = this.profile.level;//assigned straight in, so the level setter did not run
		const views = await this.profileStore.loadClassArray<LogView>( `logs/views`, LogView, LogView.schema );
		this.views.set( [ LogView.default(), ...views ] );
		this.viewIndex.set( Math.min(ProfileStore.viewIndex("logs"), this.views().length - 1) );
		await this.load();//load() sends the profile's level, so the first page already matches the combo
	}
	ngOnDestroy(){
		//on the way out: warn rather than snackbar a page the user has already left, but handle the rejection.
		this.profileStore.save<LogSettings>( "logs", this.profile ).catch( e=>console.warn("Could not save the log profile.", e) );
	}

	async load( startIndex:number=0 ){
		try{
			const entries = ( await this.service().ql<{logs: LogEntriesRest}>( this.view().query(startIndex,this.level), (m)=>console.log(m) ) ).logs;
			if( entries?.entries?.length )//was Object.keys(entries).length, always >=2 ({entries,strings}); check the actual entry count
				this.push( new LogEntries(entries) );
			this.data.setPage( startIndex );
			this.isLoading.set( false );
		}
		catch(e){
			this.snackBar.exception( "Could not load log entries.", e );
		}
	}

	push( entries:LogEntries ){
		this.data.addLoadedEntries( entries );
	}
	//re-read from the top.  the pager owns its own startIndex, so reset it too or its range keeps reading
	//"49 - 72" over the first page.  set(), not onFirstPage(), which would emit and load a second time.
	refresh(){
		this.data.clear();
		this.paginator()?.startIndex.set( 0 );
		this.load();
	}
	//minimum level to show - the level goes into the query, so re-read instead of filtering what is loaded.
	onMinLevelChange( level:ELogLevel ){
		this.level = level;
		this.refresh();
	}
	levels = [
		{ value: ELogLevel.Trace, name: "Trace" },
		{ value: ELogLevel.Debug, name: "Debug" },
		{ value: ELogLevel.Information, name: "Information" },
		{ value: ELogLevel.Warning, name: "Warning" },
		{ value: ELogLevel.Error, name: "Error" },
		{ value: ELogLevel.Critical, name: "Critical" }
	];

	onSort(sort: Sort|any){
		let sortedView = new LogView( this.view() );
		let newSort = sortedView.sort;
		let applySort =()=>{
			sortedView.sort = newSort;
			sortedView.type = ViewType.Adhoc;
			let newViews = [...this.views()];
			let index = newViews.findIndex( v => v.name==sortedView.name && v.type==sortedView.type );
			if( index==-1 ){
				newViews.push( sortedView );
				this.viewIndex.set( newViews.length - 1 );
				index = newViews.length - 1;
			}else
				newViews[index] = sortedView;

			this.views.set( [...this.views().filter( v => v.name!=sortedView.name || v.type!=sortedView.type ), sortedView] );
			this.viewIndex.set( this.views().length - 1 );
			this.data.clear();
			this.load();
		};
		if( !sort.direction ){
			if( sort.active=="time" )
				sort.direction = "asc";
			else{
				newSort.shift();
				applySort();
				return;
			}
		}
		let existingIndex = newSort.findIndex( s=>s.active==sort.active );
		if( existingIndex>0 ){
			newSort.splice( existingIndex, 1 );
			newSort.unshift( sort );
		}
		else if( existingIndex==0 )
			newSort[0] = sort;
		else
			newSort.unshift( sort );//column not yet in the sort list: make it primary (was a silent no-op)
		applySort();
	}
	onPagerChange( event:PageEvent ){
		if(	this.data.setPage(event.startIndex) )
 			this.load( this.data.allEntries.length );
	}
	onViewChange(index:number){
		const selected = this.views()[index];
		if( !selected )
			return;//a stale index must not strand view() undefined - every path below re-derives from `selected`
		//Resolve the view by identity, then re-find its index AFTER the filter.  Switching to a saved view still discards
		//the adhoc entry, but the adhoc one is itself selectable: it sits last, so dropping it and then applying the
		//pre-filter index left viewIndex==views().length, view() undefined, and load()/displayedColumns threw.
		const remaining = this.views().filter( v=>v.type!=ViewType.Adhoc || v===selected );
		const newIndex = remaining.indexOf( selected );
		this.views.set( remaining );
		this.viewIndex.set( newIndex );
		ProfileStore.setViewIndex( "logs", newIndex );//the stale index was persisted too, so the bad selection survived a reload (clamped, landing on the wrong view)
		this.data.clear();
		this.load();
	}
	//ql-list-settings emits a base View;  storing it unwrapped left view() without LogView.query, so every later
	//load sent View.query's flat column shape, which the log query answers with {} - no entries.
	async onViewSave(saved:View){
		let view = new LogView( saved );
		if( (view.isSystem || view.isAdhoc) && !this.views().find(v=>v.name==view.name && v.isSystem) )
			view.type = ViewType.User;
		let newViews = this.views().filter( v=>v.type!=ViewType.Adhoc );
		let newIndex = newViews.findIndex( v=>v.name==view.name && view.type==v.type );
		if( newIndex==-1 ){
			newViews.push( view );
			newIndex = newViews.length - 1;
		}else
			newViews[newIndex] = view;
		this.views.set( newViews );
		this.viewIndex.set( newIndex );
		verify( view.type==ViewType.User );
		if( view.type==ViewType.User )
			this.profileStore.save( `logs/views`, newViews.filter(v=>v.isUser).map(v=>v.toJson(undefined)) )
				.catch( e=>this.snackBar.exception("Could not save view.", e) );

		this.data.clear();
		this.load();
		this.isSettings.set( false );
	}
	onViewShow(view:View){//base View, as onViewSave - the `new LogView` below is what keeps query() overridden.
		this.data.clear();
		this.isSettings.set( false );
		if( view.name?.endsWith("*") && view.isAdhoc )
			view.name = view.name.substring( 0, view.name.length-1 );
		view.type = ViewType.Adhoc;
		let existing = this.views().findIndex( v=>v.name==view.name && view.type==v.type );
		let newView = new LogView( view );
		let newViews = [...this.views()];//new array + views.set so the signal notifies — in-place mutation only refreshed consumers when viewIndex happened to change
		let newIndex;
		if( existing>=0 ){
			newViews[existing] = newView;
			newIndex = existing;
		}
		else{
			newViews.push( newView );
			newIndex = newViews.length - 1;
		}
		this.views.set( newViews );
		this.viewIndex.set( newIndex );
		this.load();
	}
	onViewDelete(view:LogView){
		if( view.type!=ViewType.User ){//was `verify(...)`, i.e. `debugger;` + throw on a path the UI could reach - a frozen page rather than a no-op.  The Delete button is disabled for non-User views now; this is the belt to that braces.
			console.warn( `onViewDelete: ignoring a non-user view ('${view.name}') - only saved views can be deleted.` );
			this.isSettings.set( false );
			return;
		}
		const current = this.views()[this.viewIndex()];
		//`|| v.type!=view.type` left the deleted view's Adhoc twin behind — an unsaved edit of a view that no longer exists,
		//and the present-but-not-current adhoc that onViewChange used to choke on.  Match the adhoc form too, but only for
		//this name, so an unrelated adhoc (or a same-named System view) is untouched.
		const remaining = this.views().filter( v=>!(v.name==view.name && (v.type==view.type || v.type==ViewType.Adhoc)) );
		const newIndex = Math.max( remaining.indexOf(current), 0 );//keep the selection when it survived; the old code jumped to 0 even when the deleted view was not the current one
		this.views.set( remaining );
		this.viewIndex.set( newIndex );
		ProfileStore.setViewIndex( "logs", newIndex );//never persisted here, so a reload could restore an index pointing at a different view
		this.profileStore.save( `logs/views`, remaining.filter(v=>v.isUser).map(v=>v.toJson(undefined)) )
			.catch( e=>this.snackBar.exception("Could not delete view.", e) );
		if( current!=remaining[newIndex] ){//the selection actually moved — without this the table keeps the deleted view's rows under another view's columns
			this.data.clear();
			this.load();
		}
		this.isSettings.set( false );//onViewSave/onViewShow both close; this one left the user editing a view that no longer exists (ql-list's onViewDelete already closed)
	}

	cellClick( entry:Entry ){
		let current =	this.selectedEntry;
		if( current != entry )
			entry.selected = true;
		if( current )
			current.selected = false;
	}
	hideSelectedMessage(){
		//the two `profile.level` writes that used to bracket this push were leftover experiment code: they clobbered the level
		//the user picked (ngOnDestroy persists it) and, by assigning the field directly, bypassed the `level` setter that keeps
		//data.filter.level in step — so the combo and the row filter disagreed until the next filterData().
		this.profile.hiddenMessages.push( this.selectedEntry!.templateId );
		this.filterData();
	}
	clearHiddenMessages(){
		this.profile.hiddenMessages.length=0;
		this.filterData();
	}
	filterData(){
		if( this.data.filterData({messageIds: this.profile.hiddenMessages, message: this.filter, level: this.level}) )
			this.load( this.data.allEntries.length );
	}
	//navigate to next message with same template id
	async navigateNext(){
		if( !this.data.selectNext() ){
			await this.load( this.data.allEntries.length );
			if( !this.data.selectNext() )
				this.snackBar.warn( "No more instances found." );
		}
	}
	applyFilter( value:string ){
		this.filter = value;
		this.filterData();
	}
	get sort(){return this.view().sort;}
	service = input.required<IGraphQL>();
	profile!:LogSettings;
	data!: LogDataSource;
	displayedColumns = computed( () => {
		return this.view().fields.filter(f=>f.displayed).map( (f)=>f.name );
	} );
	//configuration = { displayHeader:true }
	@ViewChild('mainTable',{static: false}) _table!:MatTable<Entry>;
	paginator = viewChild( Paginator );//absent while the settings pane is up

	toLevel( level:ELogLevel ):string{
		switch( level ){
			case ELogLevel.Trace: return "Trc";
			case ELogLevel.Debug: return "Dbg";
			case ELogLevel.Information: return "Inf";
			case ELogLevel.Warning: return "Wrn";
			case ELogLevel.Error: return "Err";
			case ELogLevel.Critical: return "Crt";
		}
		return "";
	}
	//lands on the level chip, not the cell - the "table-row" it used to carry was redundant, mat-row is already given it
	levelClass(row:Entry){
		let className = "";
		//const levelValue = ELogLevel[row.level as keyof typeof ELogLevel];
		switch( row.level ){
			case ELogLevel.Trace: className = "log-trace"; break;
			case ELogLevel.Debug: className = "log-debug"; break;
			case ELogLevel.Information: className = "log-information"; break;
			case ELogLevel.Warning: className = "log-warning"; break;
			case ELogLevel.Error: className = "log-error"; break;
			case ELogLevel.Critical: className = "log-critical"; break;
		}
		return className;
	}
	message(entry:Entry):string{
		return this.data.message(entry);
	}

	//same order the settings page spells a combined tag in, so the transport leads - the cell clips its tail, and the
	//leading chip is the one worth keeping
	tags(entry:Entry):string[]{
		return LogEntries.orderTags( entry.tags ?? [] );
	}
	tagName(tag:string):string{
		return LogEntries.tagName( tag );
	}
	//the chips are clipped rather than wrapped, so the whole set goes on the cell's tooltip
	tagList(entry:Entry):string{
		return this.tags(entry).map( t=>LogEntries.tagName(t) ).join( ", " );
	}

	fileName(entry:Entry):string{
		return this.data.file(entry) ?? "";
	}
	functionName(entry:Entry):string{
		return this.data.function(entry) ?? "";
	}
	//measured on hover, before the browser's tooltip delay elapses, so only clipped text gets a tooltip.
	overflowTitle( event:Event ){
		const el = event.currentTarget as HTMLElement;
		el.title = el.scrollWidth>el.clientWidth ? el.innerText.trim() : "";
	}

	get columns():Record<string,string>{ return LogEntries.columns; }
	private filter!:string; 	//get filter(){return _filter;} set filter(value){ this._filter = value.trim().toLowerCase(); }
	isLoading = signal<boolean>( true );
	isSettings = signal<boolean>( false );
	//push() hides arriving rows below data.filter.level, so the setter writes both.  otherwise widening the combo
	//re-reads the lower levels and they are hidden on arrival against the level that was in force before.
	get level():ELogLevel{ return this.profile.level; } set level( value:ELogLevel ){ this.profile.level=value; this.data.filter.level=value; }
	schema:TableSchema = LogView.schema;
	//get selectedIndex(){ return this.selectedEntry?.index; } set selectedIndex(x){ this.selectedEntry = this.data.entries.find( (y)=>y.index==x ); }
	get selectedEntry(){ return this.data.allEntries.find( (e)=>e.selected ); }
	views = signal<LogView[]>(null as any);
	view = computed<LogView>( () => this.views()[this.viewIndex()] );
	get viewCopy(){ return new LogView( this.view() ); }
	viewIndex = signal<number>(null as any);
	profileStore = inject(ProfileStore);
}
