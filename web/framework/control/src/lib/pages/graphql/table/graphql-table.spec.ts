import { signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { SelectionModel } from '@angular/cdk/collections';
import { LocalPrefs, ProfileStore } from 'jde-spa';
import { SnackbarService } from '../../../shared/snackbar/snackbar-service';
import { Flex, Style, View } from '../../../model/ql/view';
import { ViewSet } from '../../../model/ql/view-set';
import { CellText, GraphQLTable } from './graphql-table';
import { object, required, scalar, schemaOf, snackbarStub } from 'jde-framework/testing';
import { flush, stable } from 'jde-spa/testing';
import { By } from '@angular/platform-browser';
import { CdkDragDrop, CdkDropList } from '@angular/cdk/drag-drop';
import { ViewColumns } from '../../../shared/view-columns/view-columns';

//The gateway's Connections list: a text column, and the grafted count that reads as a number.
const schema = schemaOf( "ServerConnection", required("name"), scalar("deleted", "DateTime"), object("opcSessions", "OpcSessions") );

const build = ( columns:any[], rows:any[] = [], tableSchema=schema )=>{
	TestBed.resetTestingModule();//a test may build two
	TestBed.configureTestingModule({ providers: [{ provide: SnackbarService, useValue: snackbarStub() }] });
	const fixture = TestBed.createComponent( GraphQLTable );
	fixture.componentRef.setInput( 'dataSource', rows );
	fixture.componentRef.setInput( 'displayedFields', View.fromTable({columns, sort: "name"}, tableSchema).fields );
	fixture.componentRef.setInput( 'selections', new SelectionModel<any>(false, []) );
	return fixture;
};
const create = ( columns:any[] )=>build( columns ).componentInstance;

//A heading has to sit over its values.  Both cells are flex containers and the same columnStyle() output goes on each,
//so an alignment that only set text-align would leave the content wherever justify-content had already put it.
describe( 'GraphQLTable.columnStyle', ()=>{
	it( 'expands a right alignment to both flex and text', ()=>{
		const style = create( ["name", {name:"opcSessions", selection:"count", style: new Style({align:"right"})}] ).columnStyle( "opcSessions" );
		expect( style ).toEqual( {justifyContent: "flex-end", textAlign: "right"} );
	} );

	it( 'centres on center and falls back to the start for left', ()=>{
		const centred = create( [{name:"name", style: new Style({align:"center"})}] ).columnStyle( "name" );
		expect( centred ).toEqual( {justifyContent: "center", textAlign: "center"} );
		const left = create( [{name:"name", style: new Style({align:"left"})}] ).columnStyle( "name" );
		expect( left ).toEqual( {justifyContent: "flex-start", textAlign: "left"} );
	} );

	it( 'keeps the width alongside the alignment', ()=>{
		const style = create( [{name:"name", style: new Style({flex: new Flex(130), align:"right"})}] ).columnStyle( "name" );
		expect( style ).toEqual( {flex: "0 0 130px", justifyContent: "flex-end", textAlign: "right"} );
	} );

	it( 'is empty for a column with no style, so the stylesheet decides', ()=>{
		expect( create(["name"]).columnStyle("name") ).toEqual( {} );
	} );
} );

//The column names the tones and the stylesheet owns the colours, so a value the column did not list has to fall back to the
//theme's default chip rather than pick up someone else's colour.
describe( 'GraphQLTable chip columns', ()=>{
	const status = {name:"opcSessions", selection:"count", chip:{Connected:"ok", Idle:"neutral", Error:"error"} as const};
	it( 'maps each listed value to its tone', ()=>{
		const table = create( ["name", status] );
		expect( table.chipClass("opcSessions", {opcSessions:{name:"Connected"}} as any) ).toBe( "chip-ok" );
		expect( table.chipClass("opcSessions", {opcSessions:{name:"Idle"}} as any) ).toBe( "chip-neutral" );
		expect( table.chipClass("opcSessions", {opcSessions:{name:"Error"}} as any) ).toBe( "chip-error" );
	} );

	it( 'gives an unlisted or missing value no tone', ()=>{
		const table = create( ["name", status] );
		expect( table.chipClass("opcSessions", {opcSessions:{name:"Draining"}} as any) ).toBe( "" );
		expect( table.chipClass("opcSessions", {} as any) ).toBe( "" );
	} );

	it( 'puts the tone class on the rendered chip', async ()=>{
		const fixture = build( ["name", status], [{id: 1, name: "a", opcSessions: {name: "Connected"}}, {id: 2, name: "b", opcSessions: {name: "Draining"}}] );
		await stable( fixture, 1 );
		const chips = [...fixture.nativeElement.querySelectorAll( 'mat-chip' )] as HTMLElement[];
		expect( chips.map( c=>[...c.classList].find( n=>n.startsWith('chip-') ) ) ).toEqual( ["chip-ok", undefined] );
	} );

	//#591:  a chip on a column the box can filter is a button - a click filters on its value, and one on a value the filter holds
	//takes the term out;  a grafted column's chip is no click target
	it( 'makes a chip a click target on the columns the host names, pressed where the filter holds its value', async ()=>{
		const kinds = schemaOf( "Thing", required("name"), scalar("kind"), object("opcSessions", "OpcSessions") );
		const fixture = build( ["name", {name: "kind", chip: {}}, status], [{id: 1, name: "a", kind: "x", opcSessions: {name: "Connected"}}, {id: 2, name: "b", kind: "y", opcSessions: {name: "Idle"}}], kinds );
		fixture.componentRef.setInput( 'chipFilters', new Map( [["kind", new Set( ["y"] )]] ) );
		const clicks = vi.fn(), rows = vi.fn();
		fixture.componentInstance.onChipClick.subscribe( clicks );
		fixture.componentInstance.onRowActivate.subscribe( rows );
		await stable( fixture, 1 );
		const element = fixture.nativeElement as HTMLElement;
		const buttons = [...element.querySelectorAll<HTMLButtonElement>( 'button.chip-filter' )];
		expect( buttons.map( b=>[b.textContent!.trim(), b.getAttribute( "aria-label" ), b.getAttribute( "aria-pressed" )] ) ).toEqual( [["x", "Filter by x", "false"], ["y", "Filter by y", "true"]] );
		expect( element.querySelectorAll( '.mat-column-opcSessions button' ).length ).toBe( 0 );
		expect( element.querySelectorAll( '.mat-column-opcSessions mat-chip' ).length ).toBe( 2 );
		buttons[0].click();
		expect( clicks.mock.calls ).toEqual( [[{column: "kind", value: "x"}]] );
		expect( rows ).not.toHaveBeenCalled();//the row is not opened
	} );

	it( 'gives a column with no chip setting no tone', ()=>{
		expect( create(["name"]).chipClass("name", {name:"Connected"} as any) ).toBe( "" );
	} );

	//Both buckets feed matColumnDef, so a chip column left in its original one is declared twice and the table throws.
	it( 'takes the column out of the bucket it would otherwise render in', ()=>{
		const table = create( ["name", status] );
		expect( table.columnNames().chip ).toEqual( ["opcSessions"] );
		expect( table.columnNames().object ).toEqual( [] );
		expect( table.columnNames().string ).toContain( "name" );
	} );

	it( 'leaves the buckets alone when nothing asks for a chip', ()=>{
		const table = create( ["name", {name:"opcSessions", selection:"count"}] );
		expect( table.columnNames().chip ).toEqual( [] );
		expect( table.columnNames().object ).toEqual( ["opcSessions"] );
	} );
} );

//The switch a route puts in the row for a table whose soft-delete flag is the feature (resources: a deleted resource is an
//unenforced one).  Rendered off the row and never off the control's own state, so a mutation that fails cannot leave the
//cell showing something the server never agreed to.
describe( 'GraphQLTable live-toggle columns', ()=>{
	const enforced = {name:"deleted", displayName:"Enforced", liveToggle:{enable:"Enforce", disable:"Stop enforcing"}};

	it( 'is on for a live row and off for a deleted one', ()=>{
		const table = create( ["name", enforced] );
		expect( table.isLive("deleted", {deleted:null} as any) ).toBe( true );
		expect( table.isLive("deleted", {} as any) ).toBe( true );
		expect( table.isLive("deleted", {deleted:new Date()} as any) ).toBe( false );
	} );

	it( 'labels the direction a click would take the row, not its current state', ()=>{
		const table = create( ["name", enforced] );
		expect( table.liveToggleLabel("deleted", {deleted:null} as any) ).toBe( "Stop enforcing" );
		expect( table.liveToggleLabel("deleted", {deleted:new Date()} as any) ).toBe( "Enforce" );
	} );

	it( 'has no label for a column that declared no toggle', ()=>{
		expect( create(["name"]).liveToggleLabel("name", {name:"a"} as any) ).toBe( "" );
	} );

	//As the chip bucket: both feed matColumnDef, so a switch column left among the dates is declared twice and mat-table throws.
	it( 'takes the column out of the date bucket', ()=>{
		const table = create( ["name", enforced] );
		expect( table.columnNames().liveToggle ).toEqual( ["deleted"] );
		expect( table.columnNames().date ).toEqual( [] );
	} );

	it( 'leaves deleted a date column when nothing asks for a switch', ()=>{
		const table = create( ["name", "deleted"] );
		expect( table.columnNames().liveToggle ).toEqual( [] );
		expect( table.columnNames().date ).toEqual( ["deleted"] );
	} );
} );


//A22:  the fields are classified once per change of displayedFields, not per change-detection pass and per cell - and each into
//one matColumnDef list at most, since a column declared twice throws.
describe( 'GraphQLTable columns', ()=>{
	it( 'puts a field in one list, a chip or a switch ahead of its type', ()=>{
		const table = create( ["name", {name:"deleted", chip:{x:"ok"}, liveToggle:{enable:"Enforce", disable:"Stop enforcing"}}, "opcSessions"] );
		expect( table.columnNames().chip ).toEqual( ["deleted"] );
		expect( Object.values(table.columnNames()).flat().sort() ).toEqual( ["deleted", "name", "opcSessions"] );
	} );

	it( 'hands a cell the same style each pass', ()=>{
		const table = create( [{name:"name", style: new Style({align:"right"})}, "deleted"] );
		expect( table.columnStyle("name") ).toBe( table.columnStyle("name") );
		expect( table.columnStyle("deleted") ).toBe( table.columnStyle("opcSessions") );//one empty style for the unstyled and the unknown
	} );

	it( 'renders the rows it is given', async ()=>{
		const fixture = build( ["name"], [{id: 1, name: "a"}, {id: 2, name: "b"}] );
		await stable( fixture, 1 );
		const cells = [...fixture.nativeElement.querySelectorAll( 'mat-cell' )].map( (c:Element)=>c.textContent!.trim() );
		expect( cells ).toEqual( ["a", "b"] );
	} );

	//a host's text for a cell, else the value:  one read from a signal redraws as the signal changes - a group's root, once placed
	it( "shows a host's text in place of the value", async ()=>{
		const placed = signal( new Map<string,string>() );
		const text:CellText = ( column, row )=>column=="name" ? placed().get( <string>row.name ) : undefined;
		const fixture = build( ["name"], [{id: 1, name: "a"}, {id: 2, name: "b"}] );
		fixture.componentRef.setInput( 'cellText', text );
		await stable( fixture, 1 );
		const cells = ()=>[...fixture.nativeElement.querySelectorAll( 'mat-cell' )].map( (c:Element)=>c.textContent!.trim() );
		expect( cells() ).toEqual( ["a", "b"] );
		placed.set( new Map( [["b", "Objects/b"]] ) );
		await stable( fixture, 1 );
		expect( cells() ).toEqual( ["a", "Objects/b"] );
	} );

	//#396:  a number cell takes the host's text too - a calendar's day start, minutes in the column, as a time of day
	it( "shows a host's text for a number", async ()=>{
		const fixture = build( ["dayStart"], [{id: 1, dayStart: 360}, {id: 2, dayStart: 0}], schemaOf( "Calendar", required("name"), required("dayStart", "UInt") ) );
		await stable( fixture, 1 );
		const cells = ()=>[...fixture.nativeElement.querySelectorAll( 'mat-cell' )].map( (c:Element)=>c.textContent!.trim() );
		expect( cells() ).toEqual( ["360", "0"] );
		fixture.componentRef.setInput( 'cellText', <CellText>(( column, row )=>column=="dayStart" ? `${<number>row['dayStart']/60}:00` : undefined) );
		await stable( fixture, 1 );
		expect( cells() ).toEqual( ["6:00", "0:00"] );
	} );
} );

//#592:  a header has its menu and its resize handle beside the sort, which is a span around the label, and each is a change to the
//view's draft that the table tells its host of
describe( 'GraphQLTable headers', ()=>{
	const things = schemaOf( "Thing", required("name"), scalar("slug"), scalar("kind") );
	//a table over a set of one view, its columns following the view as QLList's do
	const open = async ( columns:any[] = ["name", "slug", {name: "kind", style: new Style({flex: new Flex(120)})}], multiple=false )=>{
		TestBed.configureTestingModule({ providers: [{ provide: SnackbarService, useValue: snackbarStub() }] });
		const set = new ViewSet( {type: View, key: "things", profileStore: TestBed.inject( ProfileStore ), prefs: new LocalPrefs()} );
		set.reset( [View.fromTable({columns, sort: "name"}, things)], things );
		const fixture = TestBed.createComponent( GraphQLTable );
		const table = fixture.componentInstance, changes = vi.fn(), sorts = vi.fn();
		const follow = ()=>fixture.componentRef.setInput( 'displayedFields', set.current().fields.filter( f=>f.displayed ) );
		fixture.componentRef.setInput( 'dataSource', [{id: 1, name: "a", slug: "a-1", kind: "x"}] );
		fixture.componentRef.setInput( 'selections', new SelectionModel<any>(multiple, []) );
		fixture.componentRef.setInput( 'views', set );
		fixture.componentRef.setInput( 'sort', {active: "name", direction: "asc"} );
		follow();
		table.columnsChange.subscribe( ()=>{ changes(); follow(); } );
		table.onSortChange.subscribe( sorts );
		await stable( fixture, 1 );
		const element = fixture.nativeElement as HTMLElement;
		const header = ( name:string )=>element.querySelector<HTMLElement>( `mat-header-cell.mat-column-${name}` )!;
		const text = ( e:Element )=>{ const copy = e.cloneNode( true ) as Element; copy.querySelectorAll( 'mat-icon' ).forEach( i=>i.remove() ); return copy.textContent!.trim(); };
		const menu = async ( name:string )=>{
			const button = header( name ).querySelector<HTMLButtonElement>( 'button.column-menu' )!;
			button.click();
			await stable( fixture, 1 );
			return [...document.getElementById( button.getAttribute( "aria-controls" )! )!.querySelectorAll<HTMLButtonElement>( '.mat-mdc-menu-item' )];//its own panel:  one closed may still be in the overlay
		};
		const pick = async ( name:string, entry:string )=>{
			( await menu( name ) ).find( i=>text( i )==entry )!.click();
			await stable( fixture, 1 );
		};
		//a column's menu read and closed again:  its entries, or those greyed out
		const entries = async ( name:string, disabled=false )=>{
			const y = ( await menu( name ) ).filter( i=>!disabled || i.disabled ).map( text );
			document.querySelector<HTMLElement>( '.cdk-overlay-backdrop' )!.click();
			await stable( fixture, 1 );
			return y;
		};
		const shown = ()=>[...element.querySelectorAll( 'mat-header-cell' )].map( h=>text( h ) );
		const pointer = ( name:string, type:string, clientX=0 )=>header( name ).querySelector( '.column-resize' )!.dispatchEvent( new MouseEvent( type, {clientX, bubbles: true, cancelable: true} ) );
		return { fixture, set, element, changes, sorts, header, entries, pick, shown, pointer };
	};
	beforeEach( ()=>localStorage.clear() );

	it( 'gives a column its menu button and its handle, and the select column neither', async ()=>{
		const { element, header, entries } = await open( undefined, true );
		expect( element.querySelectorAll( 'button.column-menu' ).length ).toBe( 3 );
		expect( element.querySelectorAll( '.column-resize' ).length ).toBe( 3 );
		expect( element.querySelector( 'mat-header-cell.mat-column-select .column-menu' ) ).toBeNull();
		expect( header( "slug" ).querySelector( 'button.column-menu' )!.getAttribute( "aria-label" ) ).toBe( "Slug column menu" );
		expect( await entries( "slug" ) ).toEqual( ["Move left", "Move right", "Wider", "Narrower", "Hide field"] );
	} );

	it( 'sorts from a span around the label, and says the sort on the cell alone', async ()=>{
		const { header, sorts } = await open();
		await flush();//the observer that takes Material's aria-sort off the span
		const label = header( "name" ).querySelector( '.column-label' )!;
		expect( label.tagName ).toBe( "SPAN" );
		expect( label.hasAttribute( "aria-sort" ) ).toBe( false );
		expect( header( "name" ).getAttribute( "aria-sort" ) ).toBe( "ascending" );
		expect( header( "slug" ).getAttribute( "aria-sort" ) ).toBe( "none" );
		header( "slug" ).querySelector<HTMLElement>( '.mat-sort-header-container' )!.click();
		expect( sorts.mock.calls ).toEqual( [[{active: "slug", direction: "asc"}]] );
	} );

	it( 'leaves the sort alone on a click on the menu button or the handle', async ()=>{
		const { header, sorts, pointer, fixture } = await open();
		header( "slug" ).querySelector<HTMLButtonElement>( 'button.column-menu' )!.click();
		pointer( "slug", "click" );
		await stable( fixture, 1 );
		expect( header( "slug" ).querySelector( '.mat-sort-header-container button, .mat-sort-header-container .column-resize' ) ).toBeNull();//beside the sort's button, not in it
		expect( sorts ).not.toHaveBeenCalled();
	} );

	it( 'hides a field on the draft, and tells its host', async ()=>{
		const { set, changes, pick, shown } = await open();
		await pick( "slug", "Hide field" );
		expect( shown() ).toEqual( ["Name", "Kind"] );
		expect( [set.current().isAdhoc, changes.mock.calls.length] ).toEqual( [true, 1] );
	} );

	it( 'keeps the last field', async ()=>{
		const { entries } = await open( ["name"] );
		expect( await entries( "name", true ) ).toEqual( ["Move left", "Move right", "Hide field"] );
	} );

	it( 'moves a field left and right, and offers neither past an end', async ()=>{
		const { entries, pick, shown } = await open();
		expect( await entries( "name", true ) ).toEqual( ["Move left"] );
		expect( await entries( "kind", true ) ).toEqual( ["Move right"] );
		await pick( "name", "Move right" );
		expect( shown() ).toEqual( ["Slug", "Name", "Kind"] );
		await pick( "kind", "Move left" );
		expect( shown() ).toEqual( ["Slug", "Kind", "Name"] );
	} );

	it( 'moves a header dropped on another place', async ()=>{
		const { fixture, shown, changes } = await open();
		const list = fixture.debugElement.query( By.directive( ViewColumns ) ).injector.get( CdkDropList );
		list.dropped.emit( {item: {data: "kind"}, previousIndex: 2, currentIndex: 0} as CdkDragDrop<unknown> );
		await stable( fixture, 1 );
		expect( shown() ).toEqual( ["Kind", "Name", "Slug"] );
		list.dropped.emit( {item: {data: "kind"}, previousIndex: 0, currentIndex: 0} as CdkDragDrop<unknown> );//where it was
		expect( changes ).toHaveBeenCalledTimes( 1 );
	} );

	it( "writes the handle's drag to the style, header and cell alike, held at the floor, and a double-click restores the route's", async ()=>{
		const { fixture, set, element, header, pointer, sorts } = await open();
		const width = ( name:string )=>set.current().fields.find( f=>f.name==name )!.flex;
		const drawn = ( name:string )=>[header( name ).style.flex, element.querySelector<HTMLElement>( `mat-cell.mat-column-${name}` )!.style.flex];
		pointer( "kind", "pointerdown", 500 );
		pointer( "kind", "pointermove", 580 );//from the 120px the route gives it:  nothing is laid out here
		pointer( "kind", "pointerup" );
		await stable( fixture, 1 );
		expect( width( "kind" ) ).toBe( "0 0 200px" );
		expect( drawn( "kind" ) ).toEqual( ["0 0 200px", "0 0 200px"] );
		pointer( "kind", "pointermove", 0 );//the drag is over
		expect( width( "kind" ) ).toBe( "0 0 200px" );
		pointer( "kind", "pointerdown", 500 );
		pointer( "kind", "pointermove", 100 );
		pointer( "kind", "pointerup" );
		expect( width( "kind" ) ).toBe( "0 0 64px" );
		pointer( "kind", "dblclick" );
		pointer( "slug", "pointerdown", 0 );
		pointer( "slug", "pointermove", 150 );
		pointer( "slug", "dblclick" );
		await stable( fixture, 1 );
		expect( [width( "kind" ), width( "slug" )] ).toEqual( ["0 0 120px", undefined] );
		expect( drawn( "slug" ) ).toEqual( ["", ""] );
		expect( set.current().isAdhoc ).toBe( false );//as the route has it:  no draft
		expect( sorts ).not.toHaveBeenCalled();
	} );

	//reviews-issue/592-views-fields.md #5:  every move of the pointer was a draft, written to the local store
	it( "draws the handle's drag as it goes, and writes the draft once, where the pointer is let go", async ()=>{
		const { fixture, set, header, pointer, changes } = await open();
		const width = ()=>set.current().fields.find( f=>f.name=="kind" )!.flex;
		pointer( "kind", "pointerdown", 500 );
		pointer( "kind", "pointermove", 520 );
		pointer( "kind", "pointermove", 540 );
		await stable( fixture, 1 );
		expect( [header( "kind" ).style.flex, width(), set.current().isAdhoc, changes.mock.calls.length] ).toEqual( ["0 0 160px", "0 0 120px", false, 0] );
		pointer( "kind", "pointerup" );
		await stable( fixture, 1 );
		expect( [header( "kind" ).style.flex, width(), set.current().isAdhoc, changes.mock.calls.length] ).toEqual( ["0 0 160px", "0 0 160px", true, 1] );
		pointer( "kind", "pointerdown", 500 );
		pointer( "kind", "pointermove", 400 );
		pointer( "kind", "pointercancel" );
		await stable( fixture, 1 );
		expect( [header( "kind" ).style.flex, width(), changes.mock.calls.length] ).toEqual( ["0 0 160px", "0 0 160px", 1] );
	} );

	//reviews-issue/592-views-fields.md #9:  the fields the host gives lag the draft while a read is out, and their width outranked it
	it( "draws a width from the draft, ahead of the fields its host gives", async ()=>{
		const { fixture, set, header, element } = await open();
		set.edit( view=>view.setWidth( "kind", 200 ) );//the host not told:  its read is out
		await stable( fixture, 1 );
		expect( [header( "kind" ).style.flex, element.querySelector<HTMLElement>( 'mat-cell.mat-column-kind' )!.style.flex] ).toEqual( ["0 0 200px", "0 0 200px"] );
	} );

	it( 'widens and narrows by the menu, to the floor and no further', async ()=>{
		const { set, pick, entries } = await open();
		const width = ()=>set.current().fields.find( f=>f.name=="kind" )!.flex;
		await pick( "kind", "Wider" );
		expect( width() ).toBe( "0 0 152px" );
		await pick( "kind", "Narrower" );
		await pick( "kind", "Narrower" );
		await pick( "kind", "Narrower" );
		expect( width() ).toBe( "0 0 64px" );
		expect( await entries( "kind", true ) ).toEqual( ["Move right", "Narrower"] );
	} );

	it( 'pins the name, header and cell', async ()=>{
		const { element } = await open( ["slug", "name"] );
		expect( [...element.querySelectorAll( '.jde-pinned' )].map( e=>[e.tagName, e.classList.contains( "mat-column-name" )] ) ).toEqual( [["MAT-HEADER-CELL", true], ["MAT-CELL", true]] );
	} );

	it( 'only sorts where it is given no views', async ()=>{
		const fixture = build( ["name"], [{id: 1, name: "a"}] );
		await stable( fixture, 1 );
		const element = fixture.nativeElement as HTMLElement;
		expect( element.querySelector( '.mat-sort-header-container' ) ).not.toBeNull();
		expect( element.querySelector( '.column-menu, .column-resize, .jde-pinned' ) ).toBeNull();
	} );
} );