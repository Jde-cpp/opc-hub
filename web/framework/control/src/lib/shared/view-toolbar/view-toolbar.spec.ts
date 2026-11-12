import { Component, signal } from '@angular/core';
import { ComponentFixture, TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import { CdkDragDrop } from '@angular/cdk/drag-drop';
import { MatTabNavPanel } from '@angular/material/tabs';
import { IPROFILE_SERVICE, LocalPrefs, ProfileStore } from 'jde-spa';
import { Style, View } from '../../model/ql/view';
import { ViewSet } from '../../model/ql/view-set';
import { SnackbarService } from '../snackbar/snackbar-service';
import { ViewToolbar } from './view-toolbar';
import { flush, MockProfileService, screenSize } from 'jde-spa/testing';
import { object, required, scalar, schemaOf, snackbarStub } from 'jde-framework/testing';
import { TableSchema } from '../../model/ql/schema/table-schema';
import { LogEntries, LogView } from '../../pages/logs/log-entry';

//reviews/angular-refactor.md A2:  QLList, LogDetail and NodeChildren each carried a copy of this toolbar and its stylesheet.
//#588:  the views are tabs, the view menu on the active one's chevron and `+ New view` after them;  a pick list has a menu button.
//#589:  a view with a draft shows it under a dot, with Save and Discard;  the page size is the menu's.
const schema = schemaOf( "Thing" );
const view = ( name:string|undefined )=>{ const v = View.fromConfig( {configColumns: [], sort: []}, schema ); v.name = name; return v; };
//the profile rows of a saved set:  the views by number, and the index in their order
const stored = ( views:[number, View][] )=>Object.fromEntries( [["things/views", views.map( ([number])=>number )], ...views.map( ([number, v])=>[`things/views/${number}`, v.toJson(undefined)] )] );

@Component({
	imports: [MatTabNavPanel, ViewToolbar],
	template: `<view-toolbar [views]="views" [tabPanel]="panel" [tabs]="tabs()" [paged]="paged()" [columns]="columns()" [selectable]="selectable()" [refreshing]="refreshing()" [count]="count()" (viewChange)="changes=changes+1" (refresh)="refreshes=refreshes+1">
		<button left id="add">Add</button>
		<span right id="extra">extra</span>
	</view-toolbar>
	<mat-tab-nav-panel #panel id="panel">the table</mat-tab-nav-panel>`
})
class Host{
	views:ViewSet<View> = null as any;//before the first render
	tabs = signal( true );
	paged = signal( false );
	columns = signal<Record<string,string>>( {} );
	selectable = signal( false );
	refreshing = signal( false );
	count = signal<number|undefined>( 3 );
	changes = 0;
	refreshes = 0;
}

describe( 'ViewToolbar', ()=>{
	type Options = { rows?:Record<string,unknown>; route?:View[]; schema?:TableSchema; draft?:boolean; tabs?:boolean; paged?:boolean; width?:'wide'|'extra-small'; type?:typeof View };
	//A toolbar over the route's views - Default and Users, on Users - or over the saved `rows` of the profile;  a draft of the
	//current view where asked, a pick list's for tabs false, and a host that pages.
	const create = async ( {rows, schema: tableSchema=schema, route=[view( undefined ), view( "Users" )], draft=false, tabs=true, paged=false, width='wide', type=View}:Options={} )=>{
		const service = new MockProfileService();
		for( const [key, value] of Object.entries(rows ?? {}) )
			service.rows.set( key, JSON.stringify(value) );
		const snackbar = snackbarStub();
		TestBed.configureTestingModule( {providers: [screenSize( width ), {provide: IPROFILE_SERVICE, useValue: service}, {provide: SnackbarService, useValue: snackbar}]} );
		const set = new ViewSet( {type, key: "things", profileStore: TestBed.inject( ProfileStore ), prefs: new LocalPrefs()} );
		await set.load( route, tableSchema );
		if( !rows && route.length>1 )
			set.select( 1 );
		if( draft )
			set.sort( {active: "name", direction: "asc"} );
		const fixture = TestBed.createComponent( Host );
		const host = fixture.componentInstance;
		host.views = set;
		host.tabs.set( tabs );
		host.paged.set( paged );
		fixture.detectChanges();
		return { fixture, host, set, service, snackbar, element: fixture.nativeElement as HTMLElement };
	};
	//what an element reads, its icons' ligature names left out
	const text = ( e:Element )=>{
		const copy = e.cloneNode( true ) as Element;
		copy.querySelectorAll( 'mat-icon, input' ).forEach( i=>i.remove() );
		return copy.textContent!.replace( /\s+/g, " " ).trim();
	};
	const tabs = ( element:HTMLElement )=>[...element.querySelectorAll( 'a[mat-tab-link]' )] as HTMLElement[];
	const labels = ( element:HTMLElement )=>tabs( element ).map( text );
	const activeTab = ( element:HTMLElement )=>tabs( element ).find( t=>t.classList.contains( "mdc-tab--active" ) )!;
	const chevron = ( tab:HTMLElement )=>tab.querySelector<HTMLButtonElement>( 'button.view-menu' );
	//the menus open in the overlay, the last one's entries by text, and the one picked
	const panels = ()=>[...document.querySelectorAll( '.mat-mdc-menu-panel' )];
	const items = ()=>[...panels().at( -1 )!.querySelectorAll( '.mat-mdc-menu-item' )] as HTMLButtonElement[];
	const openMenu = ( fixture:ComponentFixture<Host>, trigger:HTMLElement )=>{
		trigger.click();
		fixture.detectChanges();
		return items();
	};
	const pick = async ( fixture:ComponentFixture<Host>, name:string )=>{
		items().find( i=>text( i )==name )!.click();
		fixture.detectChanges();
		await flush();
		fixture.detectChanges();
	};
	const settle = async ( fixture:ComponentFixture<Host> )=>{ await flush(); fixture.detectChanges(); };
	const rowCount = ( element:HTMLElement )=>element.querySelector( '.row-count' )?.textContent!.trim();
	const renameBox = ( element:HTMLElement )=>element.querySelector<HTMLInputElement>( 'input.rename' );
	const key = ( target:Element, key:string )=>target.dispatchEvent( new KeyboardEvent( 'keydown', {key, bubbles: true, cancelable: true} ) );
	beforeEach( ()=>localStorage.clear() );

	describe( 'the tabs row', ()=>{
		it( 'is a tab per view, the unnamed one as Default and a draft under its dot, the current one active and controlling the panel', async ()=>{
			const { element } = await create( {draft: true} );
			expect( labels( element ) ).toEqual( ["Default", "Users, unsaved changes"] );//the draft in its view's tab
			expect( tabs( element ).map( t=>!!t.querySelector( '.draft-dot' ) ) ).toEqual( [false, true] );
			expect( tabs( element ).map( t=>t.getAttribute( "aria-selected" ) ) ).toEqual( ["false", "true"] );
			expect( tabs( element ).map( t=>t.getAttribute( "role" ) ) ).toEqual( ["tab", "tab"] );
			expect( activeTab( element ).getAttribute( "aria-controls" ) ).toBe( "panel" );
			const add = element.querySelector( '.new-view' )!;
			expect( text( add ) ).toBe( "New view" );
			expect( add.closest( 'nav' ) ).toBeNull();//after the tablist, which may hold tabs alone
		} );

		it( 'selects the tab clicked and tells the host, and says nothing for the active tab again', async ()=>{
			const { fixture, element, set, host } = await create();
			tabs( element )[0].click();
			fixture.detectChanges();
			expect( set.current().name ).toBeUndefined();
			expect( host.changes ).toBe( 1 );
			expect( labels( element ) ).toEqual( ["Default", "Users"] );
			expect( activeTab( element ) ).toBe( tabs( element )[0] );
			tabs( element )[0].click();
			expect( host.changes ).toBe( 1 );
		} );

		it( 'carries the chevron on the active tab alone, and keeps its click, its keys and a press on it from the tab', async ()=>{
			const { element } = await create();
			const [other, active] = tabs( element );
			expect( chevron( other ) ).toBeNull();
			expect( chevron( active ) ).not.toBeNull();
			const click = vi.fn(), keydown = vi.fn(), press = vi.fn();
			active.addEventListener( 'click', click );
			active.addEventListener( 'keydown', keydown );
			active.addEventListener( 'mousedown', press );
			active.addEventListener( 'touchstart', press );//either starts a drag
			chevron( active )!.dispatchEvent( new MouseEvent( 'click', {bubbles: true} ) );
			key( chevron( active )!, " " );
			chevron( active )!.dispatchEvent( new MouseEvent( 'mousedown', {bubbles: true} ) );
			chevron( active )!.dispatchEvent( new Event( 'touchstart', {bubbles: true} ) );
			expect( click ).not.toHaveBeenCalled();
			expect( keydown ).not.toHaveBeenCalled();
			expect( press ).not.toHaveBeenCalled();
		} );

		it( 'drags with the CDK:  a drop moves the tab, its draft with it', async ()=>{
			const { fixture, element, set, host } = await create( {draft: true} );
			expect( element.querySelector( 'nav.cdk-drop-list' ) ).not.toBeNull();
			expect( tabs( element ).map( t=>t.classList.contains( "cdk-drag-disabled" ) ) ).toEqual( [false, false] );
			const toolbar = fixture.debugElement.query( By.directive( ViewToolbar ) ).componentInstance as ViewToolbar;
			toolbar.onDrop( {previousIndex: 1, currentIndex: 0} as CdkDragDrop<unknown> );
			await settle( fixture );
			expect( labels( element ) ).toEqual( ["Users, unsaved changes", "Default"] );
			expect( set.current().isAdhoc ).toBe( true );
			expect( host.changes ).toBe( 0 );//the view on screen is the one it was
		} );

		it( 'adds a copy of the current view named for its number, after the last, and shows it', async ()=>{
			const { fixture, element, set, host } = await create();
			element.querySelector<HTMLButtonElement>( '.new-view' )!.click();
			await settle( fixture );
			expect( labels( element ) ).toEqual( ["Default", "Users", "View 3"] );
			expect( set.current().number ).toBe( 3 );
			expect( activeTab( element ) ).toBe( tabs( element )[2] );
			expect( host.changes ).toBe( 1 );
		} );

		//#588 review #4:  it said so again once the writes were done, the tab the user picked meanwhile taken for the change's
		it( 'tells the host of a change once, whatever tab is picked while it writes', async ()=>{
			const { fixture, element, service, host } = await create();
			let written!:()=>void;
			service.save.mockReturnValue( new Promise<void>( done=>{ written = done; } ) );
			element.querySelector<HTMLButtonElement>( '.new-view' )!.click();
			tabs( element )[1].click();//Users, while the writes are out
			expect( host.changes ).toBe( 2 );
			written();
			await settle( fixture );
			expect( host.changes ).toBe( 2 );
		} );
	} );

	describe( 'the view menu', ()=>{
		it( 'opens from the chevron with Rename, Duplicate, the moves, Save and Discard changes and Delete, the moves off at the ends and the draft entries with no draft', async ()=>{
			const { fixture, element } = await create();
			const entries = openMenu( fixture, chevron( activeTab( element ) )! );
			expect( entries.map( text ) ).toEqual( ["Rename", "Duplicate", "Move left", "Move right", "Save changes", "Discard changes", "Delete"] );
			expect( entries.map( e=>e.disabled ) ).toEqual( [false, false, false, true, true, true, false] );//Users is last
		} );

		it( 'saves the draft over its view from Save changes, and drops it from Discard changes, telling the host of each', async ()=>{
			const { fixture, element, set, host, service } = await create( {draft: true} );
			openMenu( fixture, chevron( activeTab( element ) )! );
			await pick( fixture, "Save changes" );
			expect( labels( element ) ).toEqual( ["Default", "Users"] );
			expect( [set.current().isUser, set.current().sort[0].active] ).toEqual( [true, "name"] );
			expect( JSON.parse( service.rows.get("things/views/2")! ).sort ).toEqual( [{active: "name", direction: "asc"}] );
			expect( host.changes ).toBe( 1 );
			set.sort( {active: "kind", direction: "asc"} );
			fixture.detectChanges();
			openMenu( fixture, chevron( activeTab( element ) )! );
			await pick( fixture, "Discard changes" );
			expect( labels( element ) ).toEqual( ["Default", "Users"] );
			expect( set.current().sort[0].active ).toBe( "name" );
			expect( host.changes ).toBe( 2 );
		} );

		it( "renames in place:  Enter commits, and the host hears of the view on screen changing", async ()=>{
			const { fixture, element, set, host } = await create();
			openMenu( fixture, chevron( activeTab( element ) )! );
			await pick( fixture, "Rename" );
			const box = renameBox( element )!;
			expect( box.value ).toBe( "Users" );
			expect( document.activeElement ).toBe( box );
			expect( box.closest( 'nav' ) ).toBeNull();//over the tab, not in it:  a tab's content is presentational
			expect( chevron( activeTab( element ) ) ).toBeNull();
			expect( labels( element ) ).toEqual( ["Default", "Users"] );//the tab keeps its name under the box
			box.value = "People";
			key( box, "Enter" );
			await settle( fixture );
			expect( renameBox( element ) ).toBeNull();
			expect( labels( element ) ).toEqual( ["Default", "People"] );
			expect( set.current().name ).toBe( "People" );
			expect( host.changes ).toBe( 1 );
			expect( document.activeElement ).toBe( activeTab( element ) );
		} );

		it( 'reverts a rename on Escape or a blank name, and commits on leaving the box', async ()=>{
			const { fixture, element, set, host } = await create();
			const rename = async ()=>{
				openMenu( fixture, chevron( activeTab( element ) )! );
				await pick( fixture, "Rename" );
				return renameBox( element )!;
			};
			let box = await rename();
			box.value = "People";
			key( box, "Escape" );
			await settle( fixture );
			expect( renameBox( element ) ).toBeNull();
			expect( set.current().name ).toBe( "Users" );
			box = await rename();
			box.value = "  ";
			key( box, "Enter" );
			await settle( fixture );
			expect( set.current().name ).toBe( "Users" );
			expect( host.changes ).toBe( 0 );
			box = await rename();
			box.value = "Folk";
			box.dispatchEvent( new Event( 'blur' ) );
			await settle( fixture );
			expect( set.current().name ).toBe( "Folk" );
		} );

		//#588 review #7:  the blur's commit took the focus back to the tab, so Tab never left the box
		it( 'leaves the focus where leaving the box sent it', async ()=>{
			const { fixture, element, set } = await create();
			openMenu( fixture, chevron( activeTab( element ) )! );
			await pick( fixture, "Rename" );
			renameBox( element )!.value = "Folk";
			const refresh = element.querySelector<HTMLButtonElement>( 'button[aria-label="Refresh"]' )!;
			refresh.focus();
			await settle( fixture );
			expect( set.current().name ).toBe( "Folk" );
			expect( document.activeElement ).toBe( refresh );
		} );

		//#588 review #1:  the box opens on Default, which an unnamed view's undefined never equalled, so the set was saved
		it( "commits nothing for the name the box opened on, an unnamed view's Default among them", async ()=>{
			const { fixture, element, set, service, host } = await create();
			tabs( element )[0].click();
			fixture.detectChanges();
			openMenu( fixture, chevron( activeTab( element ) )! );
			await pick( fixture, "Rename" );
			expect( renameBox( element )!.value ).toBe( "Default" );
			key( renameBox( element )!, "Enter" );
			await settle( fixture );
			expect( set.current().name ).toBeUndefined();
			expect( service.save ).not.toHaveBeenCalled();
			expect( host.changes ).toBe( 1 );//the tab click's
		} );

		//#588 review #9:  the box went with the view object it was opened on
		it( "keeps the name box while the set puts another object in its view's place", async ()=>{
			const { fixture, element, set } = await create();
			openMenu( fixture, chevron( activeTab( element ) )! );
			await pick( fixture, "Rename" );
			set.edit( view=>{ view.limit = 50; } );
			await set.saveDraft( set.current() );
			fixture.detectChanges();
			const box = renameBox( element )!;
			expect( box ).not.toBeNull();
			box.value = "People";
			key( box, "Enter" );
			await settle( fixture );
			expect( set.current().name ).toBe( "People" );
		} );

		it( 'duplicates the view after itself under its name, and shows the copy', async ()=>{
			const { fixture, element, set, host } = await create();
			tabs( element )[0].click();
			fixture.detectChanges();
			openMenu( fixture, chevron( activeTab( element ) )! );
			await pick( fixture, "Duplicate" );
			expect( labels( element ) ).toEqual( ["Default", "Default", "Users"] );
			expect( set.current().number ).toBe( 3 );
			expect( activeTab( element ) ).toBe( tabs( element )[1] );
			expect( host.changes ).toBe( 2 );
		} );

		it( 'moves the view a place left or right', async ()=>{
			const { fixture, element, set, host } = await create();
			openMenu( fixture, chevron( activeTab( element ) )! );
			await pick( fixture, "Move left" );
			expect( labels( element ) ).toEqual( ["Users", "Default"] );
			expect( set.current().name ).toBe( "Users" );
			expect( host.changes ).toBe( 0 );
			const entries = openMenu( fixture, chevron( activeTab( element ) )! );
			expect( entries.map( e=>e.disabled ) ).toEqual( [false, false, true, false, true, true, false] );
			await pick( fixture, "Move right" );
			expect( labels( element ) ).toEqual( ["Default", "Users"] );
		} );

		it( "deletes the view, the only one of a saved set bringing the route's back", async ()=>{
			const { fixture, element, set, service, host } = await create( {rows: stored( [[1, view( "Mine" )]] )} );
			expect( labels( element ) ).toEqual( ["Mine"] );
			openMenu( fixture, chevron( activeTab( element ) )! );
			await pick( fixture, "Delete" );
			expect( labels( element ) ).toEqual( ["Default", "Users"] );
			expect( set.current().isSystem ).toBe( true );
			expect( host.changes ).toBe( 1 );
			expect( service.save.mock.calls.map( ([k, v])=>v==null ? `-${k}` : k ) ).toEqual( ["-things/views", "-things/views/1"] );
		} );

		it( "has no Delete for the route's only view", async ()=>{
			const { fixture, element } = await create( {route: [view( undefined )]} );
			expect( openMenu( fixture, chevron( activeTab( element ) )! ).map( e=>[text( e ), e.disabled] ) ).toContainEqual( ["Delete", true] );
		} );

		it( 'deletes a view with its draft', async ()=>{
			const { fixture, element, set } = await create( {draft: true} );
			openMenu( fixture, chevron( activeTab( element ) )! );
			await pick( fixture, "Delete" );
			expect( labels( element ) ).toEqual( ["Default"] );
			expect( set.current().isAdhoc ).toBe( false );
		} );

		it( 'says what failed, through the snackbar', async ()=>{
			const { fixture, element, service, snackbar } = await create();
			service.save.mockRejectedValue( new Error( 'offline' ) );
			openMenu( fixture, chevron( activeTab( element ) )! );
			await pick( fixture, "Duplicate" );
			expect( snackbar.exception ).toHaveBeenCalledWith( "Could not duplicate view.", expect.any( Error ) );
		} );
	} );

	describe( 'a pick list', ()=>{
		it( "has no tabs row:  a button in the left zone shows the view's name, and its menu offers the views alone", async ()=>{
			const { fixture, element, set, host } = await create( {tabs: false} );
			expect( element.querySelector( 'nav' ) ).toBeNull();
			const button = element.querySelector<HTMLButtonElement>( '.bar-left .view-button' )!;
			expect( text( button ) ).toBe( "Users" );
			expect( openMenu( fixture, button ).map( text ) ).toEqual( ["Views", "Save changes", "Discard changes"] );
			await pick( fixture, "Views" );
			const views = items();
			expect( views.map( text ) ).toEqual( ["Default", "Users"] );
			expect( views.map( v=>v.getAttribute( "aria-checked" ) ) ).toEqual( ["false", "true"] );
			await pick( fixture, "Default" );
			expect( set.current().name ).toBeUndefined();
			expect( host.changes ).toBe( 1 );
			expect( text( element.querySelector( '.view-button' )! ) ).toBe( "Default" );
		} );
	} );

	describe( 'the draft', ()=>{
		const centre = ( element:HTMLElement )=>[...element.querySelectorAll<HTMLButtonElement>( '.bar-center button' )];

		it( 'has Discard and Save in the centre zone while the view shown has one, and neither without', async ()=>{
			const { fixture, element, set } = await create( {draft: true} );
			expect( centre( element ).map( text ) ).toEqual( ["Discard", "Save"] );
			set.select( 0 );
			fixture.detectChanges();
			expect( centre( element ) ).toEqual( [] );
			expect( labels( element ) ).toEqual( ["Default", "Users, unsaved changes"] );//kept for the tab left
		} );

		it( 'saves it over its view, and tells the host of the view on screen', async ()=>{
			const { fixture, element, set, host, service } = await create( {draft: true} );
			centre( element )[1].click();
			await settle( fixture );
			expect( labels( element ) ).toEqual( ["Default", "Users"] );
			expect( set.current().isUser ).toBe( true );
			expect( service.save.mock.calls.map( ([k])=>k ) ).toEqual( ["things/views/1", "things/views/2", "things/views"] );//a built-in view:  the whole set
			expect( host.changes ).toBe( 1 );
			expect( centre( element ) ).toEqual( [] );
		} );

		it( 'discards it, and tells the host', async ()=>{
			const { fixture, element, set, host, service } = await create( {draft: true} );
			centre( element )[0].click();
			await settle( fixture );
			expect( labels( element ) ).toEqual( ["Default", "Users"] );
			expect( set.current().isSystem ).toBe( true );
			expect( service.save ).not.toHaveBeenCalled();
			expect( host.changes ).toBe( 1 );
		} );

		//reviews-issue/589-views-draft.md #6:  the button went with the draft, and the focus fell to the page
		it( "hands the focus to the view's tab as Save and Discard go", async ()=>{
			const { fixture, element, set } = await create( {draft: true} );
			centre( element )[1].focus();
			centre( element )[1].click();
			await settle( fixture );
			expect( document.activeElement ).toBe( activeTab( element ) );
			set.sort( {active: "name", direction: "desc"} );
			fixture.detectChanges();
			centre( element )[0].focus();
			centre( element )[0].click();
			fixture.detectChanges();
			expect( centre( element ) ).toEqual( [] );
			expect( document.activeElement ).toBe( activeTab( element ) );
		} );

		it( "hands the focus to a pick list's button as Discard goes", async ()=>{
			const { fixture, element } = await create( {draft: true, tabs: false} );
			centre( element )[0].focus();
			centre( element )[0].click();
			fixture.detectChanges();
			expect( document.activeElement ).toBe( element.querySelector( '.view-button' ) );
		} );

		it( "says a Save that failed, through the snackbar", async ()=>{
			const { fixture, element, service, snackbar } = await create( {draft: true} );
			service.save.mockRejectedValue( new Error( 'offline' ) );
			centre( element )[1].click();
			await settle( fixture );
			expect( snackbar.exception ).toHaveBeenCalledWith( "Could not save view.", expect.any( Error ) );
		} );

		it( "shows in a pick list's button and its Views", async ()=>{
			const { fixture, element } = await create( {draft: true, tabs: false} );
			const button = element.querySelector<HTMLButtonElement>( '.bar-left .view-button' )!;
			expect( text( button ) ).toBe( "Users, unsaved changes" );
			expect( centre( element ).map( text ) ).toEqual( ["Discard", "Save"] );
			openMenu( fixture, button );
			await pick( fixture, "Views" );
			expect( items().map( text ) ).toEqual( ["Default", "Users, unsaved changes"] );
		} );
	} );

	//#591:  the centre zone is the filter box, where the view takes a filter
	describe( 'the filter box', ()=>{
		class Unfiltered extends View{ override filterHost(){ return undefined; } }//the logs'
		const box = ( element:HTMLElement )=>element.querySelector<HTMLInputElement>( '.bar-center filter-box input' );
		const type = ( fixture:ComponentFixture<Host>, input:HTMLInputElement, value:string )=>{
			input.value = value;
			input.dispatchEvent( new Event('input', {bubbles: true}) );
			input.dispatchEvent( new KeyboardEvent('keydown', {key: 'Enter', bubbles: true}) );
			fixture.detectChanges();
		};

		it( "holds the view's filter, and what is typed is a change to the draft, which the host hears", async ()=>{
			const { fixture, element, set, host } = await create();
			const input = box( element )!;
			expect( input.value ).toBe( "" );
			type( fixture, input, "name:a" );
			expect( set.current().filter ).toBe( "name:a" );
			expect( set.current().isAdhoc ).toBe( true );
			expect( host.changes ).toBe( 1 );
			expect( [...element.querySelectorAll( '.bar-center > *' )].map( e=>e.tagName=="FILTER-BOX" ? "box" : text( e ) ) ).toEqual( ["box", "Discard", "Save"] );
			type( fixture, input, "name:a" );
			expect( host.changes ).toBe( 1 );
		} );

		it( "shows the filter of the view shown:  another tab's, and the saved one's after a Discard", async ()=>{
			const { fixture, element, set } = await create();
			type( fixture, box( element )!, "name:a" );
			set.select( 0 );
			fixture.detectChanges();
			expect( box( element )!.value ).toBe( "" );
			set.select( 1 );
			fixture.detectChanges();
			expect( box( element )!.value ).toBe( "name:a" );
			element.querySelectorAll<HTMLButtonElement>( '.bar-center button' )[0].click();
			fixture.detectChanges();
			expect( box( element )!.value ).toBe( "" );
			expect( set.current().isAdhoc ).toBe( false );
		} );

		//reviews-issue/591-views-filter-box.md #5:  what was typed and not yet asked for came due after a Discard, a draft again,
		//and was left out of a Save
		describe( 'with a keystroke not yet due', ()=>{
			afterEach( ()=>vi.useRealTimers() );
			const typed = ( fixture:ComponentFixture<Host>, input:HTMLInputElement, value:string )=>{
				input.value = value;
				input.dispatchEvent( new Event('input', {bubbles: true}) );
				fixture.detectChanges();
			};
			it( 'drops it on Discard', async ()=>{
				const { fixture, element, set, host } = await create( {draft: true} );
				vi.useFakeTimers();
				typed( fixture, box( element )!, "abc" );
				element.querySelectorAll<HTMLButtonElement>( '.bar-center button' )[0].click();
				fixture.detectChanges();
				await vi.advanceTimersByTimeAsync( 400 );
				fixture.detectChanges();
				expect( set.current().isAdhoc ).toBe( false );
				expect( box( element )!.value ).toBe( "" );
				expect( host.changes ).toBe( 1 );
			} );
			it( 'saves it on Save', async ()=>{
				const { fixture, element, set } = await create( {draft: true} );
				vi.useFakeTimers();
				typed( fixture, box( element )!, "abc" );
				element.querySelectorAll<HTMLButtonElement>( '.bar-center button' )[1].click();
				await vi.advanceTimersByTimeAsync( 400 );
				fixture.detectChanges();
				expect( set.current().filter ).toBe( "abc" );
				expect( set.current().isAdhoc ).toBe( false );
			} );
		} );

		it( 'is absent where the view takes no filter, Discard and Save still in the centre zone', async ()=>{
			const route = [Unfiltered.fromConfig( {configColumns: [], sort: []}, schema )];
			const { element } = await create( {route, draft: true, type: Unfiltered} );
			expect( element.querySelector( 'filter-box' ) ).toBeNull();
			expect( [...element.querySelectorAll( '.bar-center button' )].map( text ) ).toEqual( ["Discard", "Save"] );
		} );

		it( 'stands between the left zone and the right, and after both on a phone, where it is a row of its own', async ()=>{
			const zones = ( element:HTMLElement )=>[...element.querySelector( 'mat-toolbar' )!.children].map( c=>c.className.split( " " )[0] );
			expect( zones( (await create()).element ) ).toEqual( ["bar-left", "bar-center", "bar-right"] );
			TestBed.resetTestingModule();//a second toolbar, on a phone
			expect( zones( (await create( {width: 'extra-small'} )).element ) ).toEqual( ["bar-left", "bar-right", "bar-center"] );
		} );
	} );

	describe( 'Page size', ()=>{
		const sizes = async ( fixture:ComponentFixture<Host>, element:HTMLElement )=>{
			openMenu( fixture, chevron( activeTab( element ) )! );
			await pick( fixture, "Page size" );
			return items();
		};

		it( 'is not in the menu of a host with no pager', async ()=>{
			const { fixture, element } = await create();
			expect( openMenu( fixture, chevron( activeTab( element ) )! ).map( text ) ).not.toContain( "Page size" );
		} );

		it( "lists the sizes, the view's checked", async ()=>{
			const { fixture, element } = await create( {paged: true} );
			expect( openMenu( fixture, chevron( activeTab( element ) )! ).map( text )[0] ).toBe( "Page size" );
			await pick( fixture, "Page size" );
			expect( items().map( text ) ).toEqual( ["10 rows", "25 rows", "50 rows", "100 rows", "250 rows", "500 rows"] );
			expect( items().filter( i=>i.getAttribute( "aria-checked" )=="true" ).map( text ) ).toEqual( ["25 rows"] );
		} );

		it( "lists the view's own size where it is none of them, as the logs' 21", async ()=>{
			const route = [view( undefined )];
			route[0].limit = 21;
			const { fixture, element } = await create( {paged: true, route} );
			const entries = await sizes( fixture, element );
			expect( entries.map( text ) ).toEqual( ["10 rows", "21 rows", "25 rows", "50 rows", "100 rows", "250 rows", "500 rows"] );
			expect( entries.find( i=>i.getAttribute( "aria-checked" )=="true" )!.textContent ).toContain( "21 rows" );
		} );

		it( 'makes a pick a draft change, and tells the host;  the size it has is none', async ()=>{
			const { fixture, element, set, host, service } = await create( {paged: true} );
			await sizes( fixture, element );
			await pick( fixture, "100 rows" );
			expect( [set.current().limit, set.current().isAdhoc] ).toEqual( [100, true] );
			expect( labels( element ) ).toEqual( ["Default", "Users, unsaved changes"] );
			expect( host.changes ).toBe( 1 );
			expect( service.save ).not.toHaveBeenCalled();
			( fixture.debugElement.query( By.directive( ViewToolbar ) ).componentInstance as ViewToolbar ).pageSize( 100 );
			expect( host.changes ).toBe( 1 );
		} );

		it( "is in a pick list's menu too", async ()=>{
			const { fixture, element } = await create( {paged: true, tabs: false} );
			expect( openMenu( fixture, element.querySelector<HTMLButtonElement>( '.view-button' )! ).map( text ) ).toEqual( ["Views", "Page size", "Save changes", "Discard changes"] );
		} );
	} );

	//#592:  the columns are the view menu's, a checklist that stays open, each tick a change to the draft
	describe( 'Fields', ()=>{
		const things = schemaOf( "Thing", required("name"), scalar("slug"), scalar("kind"), scalar("secret") );
		const columns = { name: "Name", slug: "Slug", kind: "Sort" };//the route's, `secret` excluded
		const open = async ( selectable=false )=>{
			const route = View.fromTable( {columns: ["slug", "name"], sort: "name"}, things );
			const y = await create( {route: [route], schema: things} );
			y.host.columns.set( columns );
			y.host.selectable.set( selectable );
			y.fixture.detectChanges();
			openMenu( y.fixture, chevron( activeTab( y.element ) )! );
			await pick( y.fixture, "Fields" );
			return y;
		};
		const ticks = ()=>items().map( i=>[text( i ), i.getAttribute( "aria-checked" )] );

		it( 'is not in the menu of a host that names no columns', async ()=>{
			const { fixture, element } = await create();
			expect( openMenu( fixture, chevron( activeTab( element ) )! ).map( text ) ).not.toContain( "Fields" );
		} );

		it( "lists the host's columns, those shown first in the view's order and ticked", async ()=>{
			await open();
			expect( ticks() ).toEqual( [["Slug", "true"], ["Name", "true"], ["Sort", "false"]] );
			expect( items().every( i=>i.getAttribute( "role" )=="menuitemcheckbox" ) ).toBe( true );
		} );

		it( 'shows and hides a column on the draft at a tick, tells the host, and stays open', async ()=>{
			const { fixture, set, host } = await open();
			await pick( fixture, "Sort" );
			expect( ticks() ).toEqual( [["Slug", "true"], ["Name", "true"], ["Sort", "true"]] );
			const shown = ()=>set.current().fields.filter( f=>f.displayed ).map( f=>[f.name, f.displayName] );
			expect( shown() ).toEqual( [["slug", "Slug"], ["name", "Name"], ["kind", "Sort"]] );//under the host's name for it
			await pick( fixture, "Slug" );
			expect( ticks() ).toEqual( [["Slug", "false"], ["Name", "true"], ["Sort", "true"]] );//reviews-issue/592-views-fields.md #7:  in the order it opened in
			expect( [set.current().isAdhoc, host.changes] ).toEqual( [true, 2] );
		} );

		it( 'lists those shown first again once it is opened again', async ()=>{
			const { fixture, element } = await open();
			await pick( fixture, "Slug" );
			document.querySelector<HTMLElement>( '.cdk-overlay-backdrop' )!.click();
			await settle( fixture );
			//by the triggers' own panels:  those closed stay in the overlay
			const entries = ( trigger:Element )=>[...document.getElementById( trigger.getAttribute( "aria-controls" )! )!.querySelectorAll<HTMLButtonElement>( '.mat-mdc-menu-item' )];
			const menu = chevron( activeTab( element ) )!;
			menu.click();
			fixture.detectChanges();
			const fields = entries( menu ).find( i=>text( i )=="Fields" )!;
			fields.click();
			await settle( fixture );
			expect( entries( fields ).map( i=>[text( i ), i.getAttribute( "aria-checked" )] ) ).toEqual( [["Name", "true"], ["Slug", "false"], ["Sort", "false"]] );
		} );

		//reviews-issue/592-views-fields.md #4:  a column the view lacks came from the schema bare, without the route's width or chip
		it( "shows a column the view lacks as the route's default view has it", async ()=>{
			const route = [
				View.fromTable( {columns: ["slug", "name", {name: "kind", style: new Style( 90 ), chip: {a: "ok"}}], sort: "name"}, things ),
				View.fromConfig( {name: "Narrow", configColumns: ["slug", "name"], sort: [{active: "name", direction: "asc"}]}, things )
			];
			const { fixture, set, element, host } = await create( {route, schema: things} );
			host.columns.set( columns );
			fixture.detectChanges();
			openMenu( fixture, chevron( activeTab( element ) )! );
			await pick( fixture, "Fields" );
			await pick( fixture, "Sort" );
			const kind = set.current().fields.find( f=>f.name=="kind" )!;
			expect( [set.current().name, kind.displayed, kind.flex, kind.chip] ).toEqual( ["Narrow", true, "0 0 90px", {a: "ok"}] );
		} );

		it( 'keeps the last column shown', async ()=>{
			const { fixture } = await open();
			await pick( fixture, "Slug" );
			expect( items().filter( i=>i.disabled ).map( text ) ).toEqual( ["Name"] );
		} );

		it( 'offers Select first where the host lets its rows be ticked', async ()=>{
			const { fixture, set, host } = await open( true );
			expect( ticks()[0] ).toEqual( ["Select", "false"] );
			await pick( fixture, "Select" );
			expect( ticks()[0] ).toEqual( ["Select", "true"] );
			expect( [set.current().showSelector, set.current().isAdhoc, host.changes] ).toEqual( [true, true, 1] );
		} );
	} );

	//#593 (views spec *Sort*)
	describe( 'Sort by', ()=>{
		const things = schemaOf( "Thing", required("name"), scalar("slug"), scalar("kind"), object("owner", "Owner"), scalar("secret") );
		const columns = { name: "Name", slug: "Slug", kind: "Sort", owner: "Owner" };//the route's, `secret` excluded
		const open = async ()=>{
			const route = View.fromTable( {columns: ["slug", "name", "owner"], sort: "name"}, things );
			const y = await create( {route: [route], schema: things} );
			y.host.columns.set( columns );
			y.fixture.detectChanges();
			openMenu( y.fixture, chevron( activeTab( y.element ) )! );
			await pick( y.fixture, "Sort by" );
			return y;
		};
		//the entries but the moves:  what each reads, a sorted one with its direction
		const entries = ()=>items().filter( i=>!i.classList.contains( "sort-move" ) ).map( text );
		const move = async ( fixture:ComponentFixture<Host>, label:string )=>{
			items().find( i=>i.getAttribute( "aria-label" )==label )!.click();
			await settle( fixture );
		};
		const sorted = ( set:ViewSet<View> )=>set.current().sort.map( s=>`${s.active} ${s.direction}` );

		it( 'is not in the menu of a host that names no columns and a view with no sort', async ()=>{
			const { fixture, element } = await create();
			expect( openMenu( fixture, chevron( activeTab( element ) )! ).map( text ) ).not.toContain( "Sort by" );
		} );

		it( "lists the view's sorts with their directions, in their order, then the host's other columns the view can sort by, and No sorting", async ()=>{
			await open();
			expect( entries() ).toEqual( ["Name, ascending", "Slug", "Sort", "No sorting"] );//not Owner, a grafted object
		} );
		//reviews-issue/593-views-sort.md #8:  a sorted column was a checkbox ticked, which a click turned the other way and left ticked
		it( 'offers the columns as plain items, no checkbox among them', async ()=>{
			await open();
			expect( new Set( items().map( i=>[i.getAttribute( "role" ), i.getAttribute( "aria-checked" )].join() ) ) ).toEqual( new Set( ["menuitem,"] ) );
		} );
		//reviews-issue/593-views-sort.md #4:  the focus went to the new entry natively, and the arrow keys went on from the old one
		it( 'goes on from the entry a click made, by the arrow keys', async ()=>{
			const { fixture } = await open();
			await pick( fixture, "Sort" );
			expect( text( document.activeElement! ) ).toBe( "Sort, ascending" );
			panels().at( -1 )!.dispatchEvent( new KeyboardEvent( 'keydown', {key: "ArrowDown", keyCode: 40, bubbles: true, cancelable: true} ) );
			expect( document.activeElement!.getAttribute( "aria-label" ) ).toBe( "Move Sort up" );
		} );

		it( 'adds a column after the others at a click, turns a ticked one the other way, tells the host, and stays open', async ()=>{
			const { fixture, set, host } = await open();
			await pick( fixture, "Sort" );
			await pick( fixture, "Slug" );
			await pick( fixture, "Sort, ascending" );
			expect( entries() ).toEqual( ["Name, ascending", "Sort, descending", "Slug, ascending", "No sorting"] );
			expect( sorted( set ) ).toEqual( ["name asc", "kind desc", "slug asc"] );
			expect( [set.current().isAdhoc, host.changes] ).toEqual( [true, 3] );
		} );

		it( 'moves a ticked column up and down, the moves off at the ends, and keeps the focus on the move', async ()=>{
			const { fixture, set } = await open();
			await pick( fixture, "Slug" );
			await pick( fixture, "Sort" );
			const off = ()=>items().filter( i=>i.disabled ).map( i=>i.getAttribute( "aria-label" ) );
			expect( off() ).toEqual( ["Move Name up", "Move Sort down"] );
			await move( fixture, "Move Sort up" );
			expect( sorted( set ) ).toEqual( ["name asc", "kind asc", "slug asc"] );
			expect( document.activeElement!.getAttribute( "aria-label" ) ).toBe( "Move Sort up" );
			await move( fixture, "Move Sort up" );
			expect( sorted( set ) ).toEqual( ["kind asc", "name asc", "slug asc"] );
			expect( text( document.activeElement! ) ).toBe( "Sort, ascending" );//its move up now off:  the column's own entry
			await move( fixture, "Move Sort down" );
			expect( sorted( set ) ).toEqual( ["name asc", "kind asc", "slug asc"] );
		} );

		it( 'takes a dragged order', async ()=>{
			const { fixture, set, host } = await open();
			await pick( fixture, "Slug" );
			fixture.componentInstance.changes = 0;
			const toolbar = fixture.debugElement.query( By.directive( ViewToolbar ) ).componentInstance as ViewToolbar;
			toolbar.onSortDrop( {previousIndex: 1, currentIndex: 0, item: {data: "slug"}} as CdkDragDrop<unknown> );
			toolbar.onSortDrop( {previousIndex: 0, currentIndex: 0, item: {data: "slug"}} as CdkDragDrop<unknown> );//where it was
			expect( [sorted( set ), host.changes] ).toEqual( [["slug asc", "name asc"], 1] );
		} );

		it( 'clears the sort from No sorting, which is off with none', async ()=>{
			const { fixture, set, element } = await open();
			await pick( fixture, "No sorting" );
			expect( sorted( set ) ).toEqual( [] );
			//by the triggers' own panels:  those closed stay in the overlay
			const entries = ( trigger:Element )=>[...document.getElementById( trigger.getAttribute( "aria-controls" )! )!.querySelectorAll<HTMLButtonElement>( '.mat-mdc-menu-item' )];
			const menu = chevron( activeTab( element ) )!;
			menu.click();
			fixture.detectChanges();
			const sortBy = entries( menu ).find( i=>text( i )=="Sort by" )!;
			sortBy.click();
			await settle( fixture );
			expect( entries( sortBy ).map( i=>[text( i ), i.disabled] ) ).toEqual( [["Slug", false], ["Name", false], ["Sort", false], ["No sorting", true]] );//as Fields lists them
		} );
		//reviews-issue/593-views-sort.md #6:  `No sorting` emptied the sort, and LogView put its default's `time desc` back
		it( "leaves the logs' time as it stands, and is off where the time is all the sort", async ()=>{
			const route = LogView.default();
			route.sort = [{active: "level", direction: "asc"}, {active: "time", direction: "asc"}];
			const { fixture, set, host, element } = await create( {route: [route], schema: LogView.schema, type: LogView} );
			host.columns.set( LogEntries.columns );
			fixture.detectChanges();
			openMenu( fixture, chevron( activeTab( element ) )! );
			await pick( fixture, "Sort by" );
			await pick( fixture, "No sorting" );
			const toolbar = fixture.debugElement.query( By.directive( ViewToolbar ) ).componentInstance as ViewToolbar;
			expect( [sorted( set ), toolbar.canClearSort(), host.changes] ).toEqual( [["time asc"], false, 1] );
			toolbar.clearSort();
			expect( host.changes ).toBe( 1 );
		} );
	} );

	describe( 'the toolbar', ()=>{
		it( 'hands Refresh to the host', async ()=>{
			const { element, host } = await create();
			element.querySelector<HTMLButtonElement>( 'button[aria-label="Refresh"]' )!.click();
			expect( host.refreshes ).toBe( 1 );
		} );

		it( 'holds Refresh and runs the bar while a reload is out', async ()=>{
			const { fixture, element, host } = await create();
			const refresh = element.querySelector<HTMLButtonElement>( 'button[aria-label="Refresh"]' )!;
			const bar = element.querySelector( 'mat-progress-bar' )!;
			expect( refresh.disabled ).toBe( false );
			expect( bar.classList.contains( "mdc-linear-progress--indeterminate" ) ).toBe( false );
			host.refreshing.set( true );
			fixture.detectChanges();
			expect( refresh.disabled ).toBe( true );
			expect( bar.classList.contains( "mdc-linear-progress--indeterminate" ) ).toBe( true );
		} );

		it( 'counts the rows, and says nothing where the host gives no count', async ()=>{
			const { fixture, element, host } = await create();
			expect( rowCount( element ) ).toBe( "3 rows" );
			host.count.set( 1 );
			fixture.detectChanges();
			expect( rowCount( element ) ).toBe( "1 row" );
			host.count.set( undefined );
			fixture.detectChanges();
			expect( rowCount( element ) ).toBeUndefined();
		} );

		//#593:  the tune button went with the editor
		it( "puts the host's controls left, and Refresh right ahead of the host's and the count", async ()=>{
			const { element } = await create();
			expect( element.querySelector( '.bar-left #add' ) ).not.toBeNull();
			const right = element.querySelector( '.bar-right' )!;
			expect( [...right.children].map( c=>c.id || c.getAttribute( "aria-label" ) || c.className ) ).toEqual( ["Refresh", "extra", "row-count"] );
		} );
	} );

	//#595:  `Slice by` names the column the slice panel lists
	describe( 'Slice by', ()=>{
		const things = schemaOf( "Thing", required("name"), {name: "kind", type: {kind: "ENUM", name: "Kind"}}, scalar("size", "UInt") );
		const sliced = ()=>[View.fromConfig( {configColumns: ["name", "kind"], sort: []}, things )];
		const open = async ( options:Options={} )=>{
			const y = await create( {schema: things, route: sliced(), ...options} );
			y.host.columns.set( {name: "Name", kind: "Kind", size: "Size"} );
			y.fixture.detectChanges();
			return y;
		};
		it( "offers the columns a list is sliced by and No slicing, the view's ticked, each pick a change to the draft", async ()=>{
			const { fixture, element, set, host } = await open();
			openMenu( fixture, chevron( activeTab( element ) )! );
			await pick( fixture, "Slice by" );
			expect( items().map( i=>[text( i ), i.getAttribute( "aria-checked" )] ) ).toEqual( [["Kind", "false"], ["Size", "false"], ["No slicing", "true"]] );
			await pick( fixture, "Size" );
			expect( [set.current().sliceBy, set.current().isAdhoc, host.changes] ).toEqual( ["size", true, 1] );
			fixture.debugElement.query( By.directive(ViewToolbar) ).componentInstance.sliceBy( undefined );//the menu's No slicing
			expect( [set.current().sliceBy, set.current().isAdhoc, host.changes] ).toEqual( [undefined, false, 2] );
		} );
		it( 'is not in the menu of a list with no column to slice by, as the logs', async ()=>{
			const { fixture, element } = await open( {type: LogView, route: [LogView.fromConfig( {configColumns: ["name", "kind"], sort: []}, things )]} );
			expect( openMenu( fixture, chevron( activeTab( element ) )! ).map( text ) ).not.toContain( "Slice by" );
		} );
	} );

	//#596:  `Group by` names the column the rows are grouped by, and `Field sum` the numbers a group's header sums
	describe( 'Group by and Field sum', ()=>{
		const things = schemaOf( "Thing", required("name"), {name: "kind", type: {kind: "ENUM", name: "Kind"}}, scalar("size", "UInt") );
		const open = async ( options:Options={} )=>{
			const y = await create( {schema: things, route: [View.fromConfig( {configColumns: ["name", "kind"], sort: []}, things )], ...options} );
			y.host.columns.set( {name: "Name", kind: "Kind", size: "Size"} );
			y.fixture.detectChanges();
			return { ...y, toolbar: y.fixture.debugElement.query( By.directive(ViewToolbar) ).componentInstance as ViewToolbar };
		};
		it( "offers the columns a list is grouped by and No grouping, the view's ticked, each pick a change to the draft", async ()=>{
			const { fixture, element, set, host, toolbar } = await open();
			openMenu( fixture, chevron( activeTab( element ) )! );
			await pick( fixture, "Group by" );
			expect( items().map( i=>[text( i ), i.getAttribute( "aria-checked" )] ) ).toEqual( [["Kind", "false"], ["Size", "false"], ["No grouping", "true"]] );
			await pick( fixture, "Size" );
			expect( [set.current().groupBy, set.current().isAdhoc, host.changes] ).toEqual( ["size", true, 1] );
			toolbar.groupBy( undefined );//the menu's No grouping
			expect( [set.current().groupBy, set.current().isAdhoc, host.changes] ).toEqual( [undefined, false, 2] );
		} );
		it( 'offers the number columns to sum, off until the view is grouped, each tick a change to the draft', async ()=>{
			const { fixture, element, set, host, toolbar } = await open();
			const entry = openMenu( fixture, chevron( activeTab( element ) )! ).find( i=>text( i )=="Field sum" )!;
			expect( [entry.disabled, toolbar.sumChoices()] ).toEqual( [true, [{name: "size", label: "Size"}]] );
			toolbar.groupBy( "kind" );
			toolbar.toggleSum( "size", new Event("click") );
			expect( [set.current().sums, set.current().isAdhoc, host.changes] ).toEqual( [["size"], true, 2] );
			toolbar.toggleSum( "size", new Event("click") );
			expect( set.current().sums ).toEqual( [] );
		} );
		it( 'has neither in the menu of a list with no column to group by, as the logs', async ()=>{
			const { fixture, element } = await open( {type: LogView, route: [LogView.fromConfig( {configColumns: ["name", "kind"], sort: []}, things )]} );
			const entries = openMenu( fixture, chevron( activeTab( element ) )! ).map( text );
			expect( [entries.includes( "Group by" ), entries.includes( "Field sum" )] ).toEqual( [false, false] );
		} );
	} );
} );