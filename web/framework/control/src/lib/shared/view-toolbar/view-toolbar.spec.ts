import { Component, signal } from '@angular/core';
import { ComponentFixture, TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import { CdkDragDrop } from '@angular/cdk/drag-drop';
import { MatTabNavPanel } from '@angular/material/tabs';
import { IPROFILE_SERVICE, LocalPrefs, ProfileStore } from 'jde-spa';
import { View } from '../../model/ql/view';
import { ViewSet } from '../../model/ql/view-set';
import { SnackbarService } from '../snackbar/snackbar-service';
import { ViewToolbar } from './view-toolbar';
import { flush, MockProfileService } from 'jde-spa/testing';
import { schemaOf, snackbarStub } from 'jde-framework/testing';

//reviews/angular-refactor.md A2:  QLList, LogDetail and NodeChildren each carried a copy of this toolbar and its stylesheet.
//#588:  the views are tabs, the view menu on the active one's chevron and `+ New view` after them;  a pick list has a menu button.
const schema = schemaOf( "Thing" );
const view = ( name:string|undefined )=>{ const v = View.fromConfig( {configColumns: [], sort: []}, schema ); v.name = name; return v; };
//the profile rows of a saved set:  the views by number, and the index in their order
const stored = ( views:[number, View][] )=>Object.fromEntries( [["things/views", views.map( ([number])=>number )], ...views.map( ([number, v])=>[`things/views/${number}`, v.toJson(undefined)] )] );

@Component({
	imports: [MatTabNavPanel, ViewToolbar],
	template: `<view-toolbar [views]="views" [tabPanel]="panel" [tabs]="tabs()" [refreshing]="refreshing()" [count]="count()" (viewChange)="changes=changes+1" (edit)="edits=edits+1" (refresh)="refreshes=refreshes+1">
		<button left id="add">Add</button>
		<span right id="extra">extra</span>
	</view-toolbar>
	<mat-tab-nav-panel #panel id="panel">the table</mat-tab-nav-panel>`
})
class Host{
	views:ViewSet<View> = null as any;//before the first render
	tabs = signal( true );
	refreshing = signal( false );
	count = signal<number|undefined>( 3 );
	changes = 0;
	edits = 0;
	refreshes = 0;
}

describe( 'ViewToolbar', ()=>{
	type Options = { rows?:Record<string,unknown>; route?:View[]; twin?:boolean; tabs?:boolean };
	//A toolbar over the route's views - Default and Users, on Users - or over the saved `rows` of the profile;  a twin of the
	//current view where asked, and a pick list's for tabs false.
	const create = async ( {rows, route=[view( undefined ), view( "Users" )], twin=false, tabs=true}:Options={} )=>{
		const service = new MockProfileService();
		for( const [key, value] of Object.entries(rows ?? {}) )
			service.rows.set( key, JSON.stringify(value) );
		const snackbar = snackbarStub();
		TestBed.configureTestingModule( {providers: [{provide: IPROFILE_SERVICE, useValue: service}, {provide: SnackbarService, useValue: snackbar}]} );
		const set = new ViewSet( {type: View, key: "things", profileStore: TestBed.inject( ProfileStore ), prefs: new LocalPrefs()} );
		await set.load( route, schema );
		if( !rows && route.length>1 )
			set.select( 1 );
		if( twin )
			set.show( set.current() );
		const fixture = TestBed.createComponent( Host );
		const host = fixture.componentInstance;
		host.views = set;
		host.tabs.set( tabs );
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
		it( 'is a tab per view, the unnamed one as Default and the twin as edited, the current one active and controlling the panel', async ()=>{
			const { element } = await create( {twin: true} );
			expect( labels( element ) ).toEqual( ["Default", "Users", "Users (edited)"] );
			expect( tabs( element ).map( t=>t.getAttribute( "aria-selected" ) ) ).toEqual( ["false", "false", "true"] );
			expect( tabs( element ).map( t=>t.getAttribute( "role" ) ) ).toEqual( ["tab", "tab", "tab"] );
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

		it( 'drags with the CDK:  a drop moves the tab, the twin alone staying put', async ()=>{
			const { fixture, element, set, host } = await create( {twin: true} );
			expect( element.querySelector( 'nav.cdk-drop-list' ) ).not.toBeNull();
			expect( tabs( element ).map( t=>t.classList.contains( "cdk-drag-disabled" ) ) ).toEqual( [false, false, true] );
			const toolbar = fixture.debugElement.query( By.directive( ViewToolbar ) ).componentInstance as ViewToolbar;
			toolbar.onDrop( {previousIndex: 1, currentIndex: 0} as CdkDragDrop<unknown> );
			await settle( fixture );
			expect( labels( element ) ).toEqual( ["Users", "Default", "Users (edited)"] );
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
		it( 'opens from the chevron with Rename, Duplicate, the moves and Delete, the moves off at the ends', async ()=>{
			const { fixture, element } = await create();
			const entries = openMenu( fixture, chevron( activeTab( element ) )! );
			expect( entries.map( text ) ).toEqual( ["Rename", "Duplicate", "Move left", "Move right", "Delete"] );
			expect( entries.map( e=>e.disabled ) ).toEqual( [false, false, false, true, false] );//Users is last
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
			await set.save( View.copy( set.current() ) );
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
			expect( entries.map( e=>e.disabled ) ).toEqual( [false, false, true, false, false] );
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

		it( "has no Delete for the route's only view, nor for a twin", async ()=>{
			const only = await create( {route: [view( undefined )]} );
			expect( openMenu( only.fixture, chevron( activeTab( only.element ) )! ).map( e=>[text( e ), e.disabled] ) ).toContainEqual( ["Delete", true] );
			TestBed.resetTestingModule();//two toolbars in one test
			const twin = await create( {twin: true} );
			expect( openMenu( twin.fixture, chevron( activeTab( twin.element ) )! ).map( e=>[text( e ), e.disabled] ) ).toContainEqual( ["Delete", true] );
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
			expect( openMenu( fixture, button ).map( text ) ).toEqual( ["Views"] );
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

	describe( 'the toolbar', ()=>{
		it( 'hands Edit and Refresh to the host', async ()=>{
			const { element, host } = await create();
			element.querySelector<HTMLButtonElement>( 'button[aria-label="Edit view"]' )!.click();
			element.querySelector<HTMLButtonElement>( 'button[aria-label="Refresh"]' )!.click();
			expect( host.edits ).toBe( 1 );
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

		it( "puts the host's controls left, and Refresh and Edit right ahead of the host's and the count", async ()=>{
			const { element } = await create();
			expect( element.querySelector( '.bar-left #add' ) ).not.toBeNull();
			const right = element.querySelector( '.bar-right' )!;
			expect( [...right.children].map( c=>c.id || c.getAttribute( "aria-label" ) || c.className ) ).toEqual( ["Refresh", "Edit view", "extra", "row-count"] );
		} );
	} );
} );