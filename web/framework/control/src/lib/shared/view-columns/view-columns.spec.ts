import { Component } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { MatSortModule, Sort } from '@angular/material/sort';
import { MatTableModule } from '@angular/material/table';
import { LocalPrefs, ProfileStore } from 'jde-spa';
import { View, ViewFieldSettings } from '../../model/ql/view';
import { ViewSet } from '../../model/ql/view-set';
import { ViewColumnCell, ViewColumnHeader, ViewColumns } from './view-columns';
import { object, required, scalar, schemaOf } from 'jde-framework/testing';
import { stable } from 'jde-spa/testing';
import { By } from '@angular/platform-browser';
import { CdkDrag } from '@angular/cdk/drag-drop';

//#592:  a hand-built table - the logs', a node's children, the watch list - sizes its columns in its sheet and binds no style, so
//the directive is on its cells as well as its headers:  a width where the view gives one, and nothing where the sheet has it.
const schema = schemaOf( "Entry", required("time"), scalar("message"), scalar("line"), scalar("ok", "Boolean"), object("owner", "Owner") );

@Component({
	imports: [MatSortModule, MatTableModule, ViewColumnCell, ViewColumnHeader, ViewColumns],
	template: `<mat-table [dataSource]="rows" [viewColumns]="views" [filterValues]="values" (columnsChange)="changes=changes+1" matSort (matSortChange)="sorts.push($event)">
		<ng-container matColumnDef="time">
			<mat-header-cell viewColumn *matHeaderCellDef> Time </mat-header-cell>
			<mat-cell viewColumn *matCellDef="let r">{{ r.time }}</mat-cell>
		</ng-container>
		<ng-container matColumnDef="message">
			<mat-header-cell viewColumn *matHeaderCellDef> Message <span class="unit">text</span></mat-header-cell>
			<mat-cell viewColumn *matCellDef="let r">{{ r.message }}</mat-cell>
		</ng-container>
		<ng-container matColumnDef="line">
			<mat-header-cell viewColumn [sortable]="false" *matHeaderCellDef> Line </mat-header-cell>
			<mat-cell viewColumn *matCellDef="let r">{{ r.line }}</mat-cell>
		</ng-container>
		<ng-container matColumnDef="ok">
			<mat-header-cell viewColumn *matHeaderCellDef> Ok </mat-header-cell>
			<mat-cell viewColumn *matCellDef="let r">{{ r.ok }}</mat-cell>
		</ng-container>
		<ng-container matColumnDef="owner">
			<mat-header-cell viewColumn *matHeaderCellDef> Owner </mat-header-cell>
			<mat-cell viewColumn *matCellDef="let r">{{ r.owner }}</mat-cell>
		</ng-container>
		<ng-container matColumnDef="actions">
			<mat-header-cell *matHeaderCellDef></mat-header-cell>
			<mat-cell *matCellDef="let r">x</mat-cell>
		</ng-container>
		<mat-header-row *matHeaderRowDef="columns()"></mat-header-row>
		<mat-row *matRowDef="let row; columns: columns()"></mat-row>
	</mat-table>`
})
class Host{
	views:ViewSet<View> = null as any;//before the first render
	rows = [{time: "10:00", message: "a", line: 1}, {time: "10:01", message: "b", line: 2}];
	columns = ()=>[...this.views.current().fields.filter( f=>f.displayed ).map( f=>f.name ), "actions"];
	values:Record<string,unknown[]> = {};
	changes = 0;
	sorts:Sort[] = [];
}

describe( 'ViewColumns on a hand-built table', ()=>{
	//`columns`:  the view's, the time, the message and the line unless given;  `values`:  the rows' by column, as the host has them
	const open = async ( columns:(string|ViewFieldSettings)[]=["time", "message", "line"], values:Record<string,unknown[]>={} )=>{
		const set = new ViewSet( {type: View, key: "entries", profileStore: TestBed.inject( ProfileStore ), prefs: new LocalPrefs()} );
		set.reset( [View.fromTable({columns, sort: "time"}, schema)], schema );
		const fixture = TestBed.createComponent( Host );
		fixture.componentInstance.views = set;
		fixture.componentInstance.values = values;
		await stable( fixture, 1 );
		const element = fixture.nativeElement as HTMLElement;
		const header = ( name:string )=>element.querySelector<HTMLElement>( `mat-header-cell.mat-column-${name}` )!;
		const cells = ( name:string )=>[...element.querySelectorAll<HTMLElement>( `mat-cell.mat-column-${name}` )];
		const pick = async ( name:string, entry:string )=>{
			header( name ).querySelector<HTMLButtonElement>( 'button.column-menu' )!.click();
			await stable( fixture, 1 );
			[...[...document.querySelectorAll( '.mat-mdc-menu-panel' )].at( -1 )!.querySelectorAll<HTMLButtonElement>( '.mat-mdc-menu-item' )].find( i=>i.textContent!.includes( entry ) )!.click();
			await stable( fixture, 1 );
		};
		//a header's menu, opened:  its entries
		const menu = async ( name:string )=>{
			header( name ).querySelector<HTMLButtonElement>( 'button.column-menu' )!.click();
			await stable( fixture, 1 );
			return [...[...document.querySelectorAll( '.mat-mdc-menu-panel' )].at( -1 )!.querySelectorAll<HTMLButtonElement>( '.mat-mdc-menu-item' )];
		};
		return { fixture, host: fixture.componentInstance, set, element, header, cells, pick, menu };
	};
	beforeEach( ()=>localStorage.clear() );

	it( "leaves a column to its sheet until the view gives it a width, then sets it on the header and every cell, and takes it off again", async ()=>{
		const { fixture, header, cells, pick, set, host } = await open();
		const drawn = ()=>[header( "message" ), ...cells( "message" )].map( e=>[e.style.flex, e.style.minWidth] );
		expect( drawn() ).toEqual( [["", ""], ["", ""], ["", ""]] );
		await pick( "message", "Wider" );
		expect( drawn() ).toEqual( [["0 0 96px", "0px"], ["0 0 96px", "0px"], ["0 0 96px", "0px"]] );//from nothing laid out, so from the floor;  over any minimum the sheet has
		expect( [header( "time" ).style.flex, set.current().isAdhoc, host.changes] ).toEqual( ["", true, 1] );
		header( "message" ).querySelector( '.column-resize' )!.dispatchEvent( new MouseEvent( 'dblclick', {bubbles: true} ) );
		await stable( fixture, 1 );
		expect( drawn() ).toEqual( [["", ""], ["", ""], ["", ""]] );
		expect( set.current().isAdhoc ).toBe( false );
	} );

	it( 'keeps what the host wrote in a header, and gives a column outside the view no menu', async ()=>{
		const { header, element } = await open();
		expect( header( "message" ).querySelector( '.column-label .unit' )!.textContent ).toBe( "text" );
		expect( header( "actions" ).querySelector( '.column-menu, .column-resize, .column-label' ) ).toBeNull();
		expect( element.querySelectorAll( 'button.column-menu' ).length ).toBe( 3 );
	} );

	//reviews-issue/592-views-fields.md #12:  the cells said the view's sort, the time, where this table's MatSort - bound to nothing -
	//drew the message's arrow
	it( 'sorts through the table, and says the sort as MatSort draws it, but for a column that does not, which says none', async ()=>{
		const { fixture, header, host } = await open();
		const said = ()=>[header( "time" ).getAttribute( "aria-sort" ), header( "message" ).getAttribute( "aria-sort" ), header( "line" ).hasAttribute( "aria-sort" )];
		expect( said() ).toEqual( ["none", "none", false] );
		header( "message" ).querySelector<HTMLElement>( '.mat-sort-header-container' )!.click();
		header( "line" ).querySelector<HTMLElement>( '.mat-sort-header-container' )!.click();
		await stable( fixture, 2 );
		expect( host.sorts ).toEqual( [{active: "message", direction: "asc"}] );
		expect( said() ).toEqual( ["none", "ascending", false] );
		expect( header( "message" ).querySelector( '.column-label' )!.hasAttribute( "aria-sort" ) ).toBe( false );
	} );

	//reviews-issue/592-views-fields.md #1:  the header row drawn again for a move or a hide took the focused menu button with it
	it( 'gives the focus to the column moved, or after a hide to its neighbour, not to the page', async ()=>{
		const { fixture, pick } = await open();
		const focused = ()=>[[...document.activeElement!.closest( 'mat-header-cell' )?.classList ?? []].find( c=>c.startsWith( "mat-column-" ) ), document.activeElement!.classList.contains( "column-menu" )];
		await pick( "time", "Move right" );
		await stable( fixture, 2 );
		expect( focused() ).toEqual( ["mat-column-time", true] );
		await pick( "time", "Hide field" );
		await stable( fixture, 2 );
		expect( focused() ).toEqual( ["mat-column-line", true] );
	} );

	//reviews-issue/592-views-fields.md #3:  Narrower measured, took 32 off and raised the result to the floor
	it( 'never widens a column its sheet draws under the floor', async ()=>{
		const { fixture, header, set } = await open();
		vi.spyOn( header( "line" ), 'getBoundingClientRect' ).mockReturnValue( {width: 45} as DOMRect );
		const handle = header( "line" ).querySelector( '.column-resize' )!;
		const pointer = ( type:string, clientX=0 )=>handle.dispatchEvent( new MouseEvent( type, {clientX, bubbles: true, cancelable: true} ) );
		header( "line" ).querySelector<HTMLButtonElement>( 'button.column-menu' )!.click();
		await stable( fixture, 1 );
		const narrower = [...[...document.querySelectorAll( '.mat-mdc-menu-panel' )].at( -1 )!.querySelectorAll<HTMLButtonElement>( '.mat-mdc-menu-item' )].find( i=>i.textContent!.includes( "Narrower" ) )!;
		expect( narrower.disabled ).toBe( true );
		document.querySelector<HTMLElement>( '.cdk-overlay-backdrop' )!.click();
		await stable( fixture, 1 );
		pointer( "pointerdown", 100 );
		pointer( "pointermove", 90 );
		pointer( "pointerup" );
		expect( set.current().isAdhoc ).toBe( false );//held where it was:  nothing written
		pointer( "pointerdown", 100 );
		pointer( "pointermove", 110 );
		pointer( "pointerup" );
		expect( set.current().fields.find( f=>f.name=="line" )!.flex ).toBe( "0 0 55px" );
	} );

	//reviews-issue/592-views-fields.md #8:  a swipe that started on the header row dragged a column and kept the page from scrolling
	it( 'drags a header by touch only after a press and hold', async ()=>{
		const { fixture } = await open();
		const drag = fixture.debugElement.query( By.css( 'mat-header-cell.mat-column-time' ) ).injector.get( CdkDrag );
		expect( drag.dragStartDelay ).toEqual( {touch: 500, mouse: 0} );
	} );

	//reviews-issue/592-views-fields.md #2:  the rows as wide as their columns, the block a pinned cell sticks in (_general.scss)
	it( 'marks the table for its rows to be as wide as its columns', async ()=>{
		const { element } = await open();
		expect( element.querySelector( 'mat-table' )!.classList ).toContain( "jde-view-columns" );
	} );

	it( 'hides and moves a column, the cells following, and pins the first where there is no name', async ()=>{
		const { element, pick, cells } = await open();
		const order = ()=>[...element.querySelectorAll( 'mat-header-cell' )].map( h=>[...h.classList].find( c=>c.startsWith( "mat-column-" ) )!.slice( 11 ) );
		expect( [...element.querySelectorAll( '.jde-pinned' )].every( e=>e.classList.contains( "mat-column-time" ) ) ).toBe( true );
		await pick( "time", "Move right" );
		expect( order() ).toEqual( ["message", "time", "line", "actions"] );
		expect( element.querySelector( 'mat-row' )!.textContent ).toBe( "a10:001x" );
		expect( cells( "message" ).every( c=>c.classList.contains( "jde-pinned" ) ) ).toBe( true );
		await pick( "message", "Hide field" );
		expect( order() ).toEqual( ["time", "line", "actions"] );
	} );

	//#593 (views spec *Sort*)
	describe( 'the sort', ()=>{
		const sorted = ( set:ViewSet<View> )=>set.current().sort.map( s=>`${s.active} ${s.direction}` );
		const entry = ( entries:HTMLButtonElement[], text:string )=>entries.find( i=>i.textContent!.includes( text ) );

		it( "is set from a header's menu:  the direction where the view sorts by the column, else the column first", async ()=>{
			const { pick, set, host } = await open();
			await pick( "message", "Sort descending" );
			expect( sorted( set ) ).toEqual( ["message desc", "time asc"] );
			await pick( "time", "Sort descending" );
			expect( sorted( set ) ).toEqual( ["message desc", "time desc"] );
			expect( [set.current().isAdhoc, host.changes] ).toEqual( [true, 2] );
		} );

		it( "says the direction the view has in a header's menu", async ()=>{
			const { menu } = await open();
			const entries = await menu( "time" );
			expect( ["Sort ascending", "Sort descending"].map( t=>[entry( entries, t )!.getAttribute( "role" ), entry( entries, t )!.getAttribute( "aria-checked" )] ) ).toEqual( [["menuitemradio", "true"], ["menuitemradio", "false"]] );
		} );

		it( 'is not offered on a column the host or the view refuses, whose header does not sort either', async ()=>{
			const { menu, header, host, fixture } = await open( ["time", "line", "owner"] );
			for( const column of ["line", "owner"] ){//the host's [sortable], and a grafted object
				expect( entry( await menu( column ), "Sort ascending" ) ).toBeUndefined();
				document.querySelector<HTMLElement>( '.cdk-overlay-backdrop' )!.click();
				await stable( fixture, 1 );
				header( column ).querySelector<HTMLElement>( '.mat-sort-header-container' )!.click();
			}
			expect( host.sorts ).toEqual( [] );
			expect( header( "owner" ).hasAttribute( "aria-sort" ) ).toBe( false );
		} );

		it( "numbers a second and later sort's header, with its arrow, and leaves the first to Material", async ()=>{
			const { fixture, header, set, element } = await open();
			const places = ()=>["time", "message", "line"].map( c=>header( c ).querySelector( '.sort-place' )?.textContent );
			expect( places() ).toEqual( [undefined, undefined, undefined] );
			set.edit( view=>{ view.toggleSort("message"); view.toggleSort("message"); view.toggleSort("line"); } );
			await stable( fixture, 1 );
			expect( places() ).toEqual( [undefined, "arrow_downward2sorted descending, sort 2", undefined] );//the line:  its header does not sort
			expect( header( "message" ).querySelector( '.sort-place [aria-hidden=true]:not(mat-icon)' )!.textContent ).toBe( "2" );
			expect( [...element.querySelectorAll( '.jde-sorted-later' )].map( e=>e.closest( 'mat-header-cell' ) ) ).toEqual( [header( "message" )] );
		} );
	} );

	//#593:  on a chip, enum or boolean column the box can filter
	describe( 'Filter by…', ()=>{
		const columns = ["time", "message", {name: "line", chip: {"1": "ok", "2": "warn"}}, "ok"] as (string|ViewFieldSettings)[];
		//the entries of a column's `Filter by…`, each with its tick
		const choices = async ( y:Awaited<ReturnType<typeof open>>, name:string )=>{
			( await y.menu( name ) ).find( i=>i.textContent!.includes( "Filter by…" ) )!.click();
			await stable( y.fixture, 1 );
			return ()=>[...[...document.querySelectorAll( '.mat-mdc-menu-panel' )].at( -1 )!.querySelectorAll<HTMLButtonElement>( '.mat-mdc-menu-item' )];
		};
		const ticks = ( entries:HTMLButtonElement[] )=>entries.map( i=>[i.textContent!.replace( /check_box(_outline_blank)?/, "" ).trim(), i.getAttribute( "aria-checked" )] );

		it( 'is on a chip or a boolean column alone', async ()=>{
			const y = await open( columns );
			const offered:boolean[] = [];
			for( const column of ["time", "message", "line", "ok"] ){
				offered.push( ( await y.menu( column ) ).some( i=>i.textContent!.includes( "Filter by…" ) ) );
				document.querySelector<HTMLElement>( '.cdk-overlay-backdrop' )!.click();
				await stable( y.fixture, 1 );
			}
			expect( offered ).toEqual( [false, false, true, true] );
		} );

		it( "lists a chip column's values and the rows', adds a pick to the filter and takes it out again, the menu staying open", async ()=>{
			const y = await open( columns, {line: [2, 7]} );
			const entries = await choices( y, "line" );
			expect( ticks( entries() ) ).toEqual( [["1", "false"], ["2", "false"], ["7", "false"]] );
			entries()[2].click();
			await stable( y.fixture, 1 );
			expect( [y.set.current().filter, y.host.changes] ).toEqual( ["Line:7", 1] );
			entries()[0].click();
			await stable( y.fixture, 1 );
			expect( y.set.current().filter ).toBe( "Line:7,1" );
			expect( ticks( entries() ) ).toEqual( [["1", "true"], ["2", "false"], ["7", "true"]] );
			entries()[2].click();
			entries()[0].click();
			await stable( y.fixture, 1 );
			expect( [y.set.current().filter, y.set.current().isAdhoc, y.host.changes] ).toEqual( ["", false, 4] );
		} );

		it( "lists true and false for a boolean column, and a value the filter holds that the rows do not", async ()=>{
			const y = await open( columns );
			y.set.edit( view=>{ view.filter = "ok:true line:9"; } );
			await stable( y.fixture, 1 );
			expect( ticks( ( await choices( y, "ok" ) )() ) ).toEqual( [["true", "true"], ["false", "false"]] );
			document.querySelectorAll<HTMLElement>( '.cdk-overlay-backdrop' ).forEach( b=>b.click() );
			await stable( y.fixture, 1 );
			expect( ticks( ( await choices( y, "line" ) )() ) ).toEqual( [["1", "false"], ["2", "false"], ["9", "true"]] );
		} );
	} );

	//#595
	it( 'slices by a boolean column from its menu, ticked on the column sliced by, and not by text', async ()=>{
		const y = await open( ["time", "message", "ok"] );
		expect( ( await y.menu( "message" ) ).some( i=>i.textContent!.includes( "Slice by this field" ) ) ).toBe( false );
		document.querySelector<HTMLElement>( '.cdk-overlay-backdrop' )!.click();
		await stable( y.fixture, 1 );
		await y.pick( "ok", "Slice by this field" );
		expect( [y.set.current().sliceBy, y.set.current().isAdhoc, y.host.changes] ).toEqual( ["ok", true, 1] );
		expect( ( await y.menu( "ok" ) ).find( i=>i.textContent!.includes( "Slice by this field" ) )!.getAttribute( "aria-checked" ) ).toBe( "true" );
		document.querySelector<HTMLElement>( '.cdk-overlay-backdrop' )!.click();
		await stable( y.fixture, 1 );
		await y.pick( "ok", "Slice by this field" );
		expect( y.set.current().sliceBy ).toBeUndefined();
	} );
} );