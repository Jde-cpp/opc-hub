import { Component, OnDestroy, OnInit, input, signal, inject, computed } from '@angular/core';
import { DatePipe } from '@angular/common';
import { MatTableModule } from '@angular/material/table';
import {MatSortModule, Sort} from '@angular/material/sort';
import { LogDataSource } from '../log-data-source';
import {LogSettings} from '../log-settings';
import { QLListSettings } from '../../ql/list/ql-list-settings/ql-list-settings';
import {SnackbarService} from '../../../shared/snackbar/snackbar-service';


import { ELogLevel } from 'jde-proto/Log';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatChip } from '@angular/material/chips';
import {MatSelectModule} from '@angular/material/select';
import { PageEvent, Paginator } from '../../../shared/paginator/paginator';
import { ViewToolbar } from '../../../shared/view-toolbar/view-toolbar';
import { LocalPrefs, ProfileStore } from 'jde-spa';
import { IGraphQL } from '../../../services/graphql';
import { TableSchema } from '../../../model/ql/schema/table-schema';
import { View } from '../../../model/ql/view';
import { ViewSet } from '../../../model/ql/view-set';
import { Entry,LogEntries, LogEntriesRest, LogView } from '../log-entry';
import { logLevel, logLevels } from '../log-levels';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatTooltipModule } from '@angular/material/tooltip';

@Component({
	selector: 'log-detail',
	templateUrl: './log-detail.html',
	styleUrls: ['./log-detail.scss'],
	imports: [DatePipe, MatButtonModule, MatChip, MatFormFieldModule, MatIconModule, MatTableModule, MatSelectModule, MatSortModule, MatTooltipModule, Paginator, QLListSettings, ViewToolbar]
})
export class LogDetail implements OnInit, OnDestroy{
	#snackBar = inject( SnackbarService );

	async ngOnInit(){
		this.data = new LogDataSource( this.view );
		this.profile = new LogSettings( await this.profileStore.load<LogSettings>("logs", new LogSettings()) );
		this.data.filter = { messageIds: this.profile.hiddenMessages, level: this.profile.level };//assigned straight in, so the level setter did not run;  the messages hidden last time stay hidden
		await this.viewSet.load( [LogView.default()], LogView.schema );
		await this.load();//load() sends the profile's level, so the first page already matches the combo
	}
	ngOnDestroy(){
		//on the way out: warn rather than snackbar a page the user has already left, but handle the rejection.
		this.profileStore.save<LogSettings>( "logs", this.profile ).catch( e=>console.warn("Could not save the log profile.", e) );
	}

	//reads on from `skip`, an offset into everything loaded, and re-shows the page the pager is on
	async load( skip:number=0 ){
		this.#loads.update( n=>n+1 );
		try{
			const query = this.view().query( skip, this.level );
			const entries = ( await this.service().ql<{logs: LogEntriesRest}>( query ) ).logs;
			const read = entries?.entries?.length ?? 0;
			if( read )//was Object.keys(entries).length, always >=2 ({entries,strings}); check the actual entry count
				this.push( new LogEntries(entries) );
			if( read<query.vars.limit )
				this.data.endReached();
			this.data.setPage( this.data.startIndex() );
			this.isLoading.set( false );
		}
		catch(e){
			this.#snackBar.exception( "Could not load log entries.", e );
		}
		finally{
			this.#loads.update( n=>n-1 );
		}
	}

	push( entries:LogEntries ){
		this.data.addLoadedEntries( entries );
	}
	//re-read from the top.  clear() puts the data source - and the pager bound to it - back on the first page.
	refresh(){
		this.data.clear();
		this.load();
	}
	//minimum level to show - the level goes into the query, so re-read instead of filtering what is loaded.
	onMinLevelChange( level:ELogLevel ){
		this.level = level;
		this.refresh();
	}
	levels = logLevels;

	//time is never left unsorted;  another column, cleared, drops out of the sort
	onSort( sort:Sort ){
		if( !sort.direction && sort.active=="time" )
			sort = { active: "time", direction: "asc" };
		if( sort.direction )
			this.viewSet.sort( sort );
		else{
			const view = LogView.copy( this.view() );
			view.sort = view.sort.filter( s=>s.active!=sort.active );
			this.viewSet.show( view );
		}
		this.refresh();
	}
	onPagerChange( event:PageEvent ){
		if( this.data.turnTo(event.startIndex) )
			this.load( this.data.allEntries.length );
	}
	onViewChange( index:number ){
		if( this.viewSet.select(index) )
			this.refresh();
	}
	onViewSave( view:View ){
		this.viewSet.save( view ).catch( e=>this.#snackBar.exception("Could not save view.", e) );
		this.refresh();
		this.isSettings.set( false );
	}
	onViewShow( view:View ){
		this.isSettings.set( false );
		this.viewSet.show( view );
		this.refresh();
	}
	onViewDelete( view:View ){
		const current = this.view();
		this.viewSet.remove( view ).catch( e=>this.#snackBar.exception("Could not delete view.", e) );
		if( this.view()!==current )//without this the table keeps the deleted view's rows under another view's columns
			this.refresh();
		this.isSettings.set( false );
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
		if( this.data.filterData({messageIds: this.profile.hiddenMessages, level: this.level}) && this.data.length()===undefined )
			this.load( this.data.allEntries.length );
	}
	//The next row of the selected one's message, read on for while the log has more.  It read one more read's worth and then said
	//there were no more (reviews-issue/web-refactor-review.md #5).  A read that comes back empty is what tells the end of a log
	//that fills its last read exactly, so the loop asks length() again rather than stopping on it.
	async navigateNext(){
		const entry = this.selectedEntry;
		while( entry && !this.data.selectNext() ){
			if( this.data.length()!==undefined ){
				this.#snackBar.warn( "No more rows of this message." );
				return;
			}
			const loaded = this.data.allEntries.length;
			await this.load( loaded );
			if( this.selectedEntry!==entry )//a refresh, a sort or a click has moved on:  reading on would read to the end of the log
				return;
			if( this.data.allEntries.length==loaded && this.data.length()===undefined )//a failed read, which load() has reported
				return;
		}
	}
	hiddenTooltip():string{
		const count = this.profile.hiddenMessages.length;
		return count ? `Show the ${count==1 ? "hidden message" : `${count} hidden messages`} again` : "No message is hidden";
	}
	get sort(){return this.view().sort;}
	service = input.required<IGraphQL>();
	profile!:LogSettings;
	data!: LogDataSource;
	displayedColumns = computed( () => {
		return this.view().fields.filter(f=>f.displayed).map( (f)=>f.name );
	} );
	//configuration = { displayHeader:true }

	toLevel( level:ELogLevel ):string{ return logLevel( level )?.short ?? ""; }
	//lands on the level chip, not the cell - the "table-row" it used to carry was redundant, mat-row is already given it
	levelClass( row:Entry ):string{ return logLevel( row.level )?.cssClass ?? ""; }
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
	isLoading = signal<boolean>( true );
	#loads = signal<number>( 0 );//a page turn can read on while a refresh is still out
	isRefreshing = computed<boolean>( ()=>this.#loads()>0 );
	isSettings = signal<boolean>( false );
	//push() hides arriving rows below data.filter.level, so the setter writes both.  otherwise widening the combo
	//re-reads the lower levels and they are hidden on arrival against the level that was in force before.
	get level():ELogLevel{ return this.profile.level; } set level( value:ELogLevel ){ this.profile.level=value; this.data.filter.level=value; }
	schema:TableSchema = LogView.schema;
	get selectedEntry(){ return this.data.allEntries.find( (e)=>e.selected ); }
	profileStore = inject(ProfileStore);
	viewSet = new ViewSet( {type: LogView, key: "logs", profileStore: this.profileStore, prefs: inject(LocalPrefs)} );
	view = this.viewSet.current;
	get viewCopy(){ return LogView.copy( this.view() ); }
}